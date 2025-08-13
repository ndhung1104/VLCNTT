#include <EEPROM.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <DNSServer.h>
// #include <FirebaseESP8266.h>
// #include <ArduinoJson.h>
#include <FirebaseESP8266.h>
#include <addons/RTDBHelper.h>
#include <time.h> 
#include <SoftwareSerial.h>

// D5 = RX (GPIO14), D6 = TX (GPIO12)
SoftwareSerial megaSerial(14, 12);
#define DATABASE_URL "https://magic-mirror-687e8-default-rtdb.asia-southeast1.firebasedatabase.app"
#define DATABASE_SECRET "NFksLgCtRTQ9QUnd62ny525LMJwLQGM6u69VI7GZ"  // từ Firebase > Realtime DB > Service accounts > Legacy Token
const bool DEBUG_MODE = 1;
bool connected = false;


FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;

#define EEPROM_SIZE      96
#define FLAG_ADDR        0
#define AP_SSID          "ESP_ConfigAP"
#define AP_PASS          "config123"
#define DNS_PORT         53
#define CONFIG_TIMEOUT   300000UL  // 5 minutes
#define WIFI_RETRY_TIME  10000UL   // 10s retry

ESP8266WebServer server(80);
DNSServer dnsServer;

struct {
  char ssid[32];
  char pass[64];
} wifiCred;

unsigned long lastConnectionCheck = 0;

String lastLed = "";
int lastMusic = -1;
unsigned long lastSensorMillis = 0;
unsigned long lastControlMillis = 0;

void loadConfig() {
  EEPROM.begin(EEPROM_SIZE);
  if (EEPROM.read(FLAG_ADDR) == 1) {
    for (int i = 0; i < 32; i++) wifiCred.ssid[i] = EEPROM.read(1 + i);
    for (int i = 0; i < 64; i++) wifiCred.pass[i] = EEPROM.read(33 + i);
  } else {
    wifiCred.ssid[0] = 0;
    wifiCred.pass[0] = 0;
  }
}

void saveConfig() {
  EEPROM.write(FLAG_ADDR, 1);
  for (int i = 0; i < 32; i++) EEPROM.write(1 + i, wifiCred.ssid[i]);
  for (int i = 0; i < 64; i++) EEPROM.write(33 + i, wifiCred.pass[i]);
  EEPROM.commit();
}

void clearConfig() {
  EEPROM.write(FLAG_ADDR, 0);
  EEPROM.commit();
}

String scanNetworksHTML() {
  int n = WiFi.scanNetworks();
  String html = "";
  for (int i = 0; i < n; i++) {
    html += "<option value='" + WiFi.SSID(i) + "'>" + WiFi.SSID(i) + " (" + String(WiFi.RSSI(i)) + "dBm)</option>";
  }
  return html;
}

void handleRoot() {
  String options = scanNetworksHTML();
  String html =
    "<!DOCTYPE html><html><head><meta name='viewport' content='width=device-width, initial-scale=1'>"
    "<style>body{font-family:sans-serif;text-align:center;padding:2em;background:#f0f0f0;}"
    "form{background:white;display:inline-block;padding:2em;border-radius:10px;box-shadow:0 0 10px rgba(0,0,0,0.1);}"
    "input,select{width:100%;padding:0.5em;margin-top:1em;font-size:1em;}</style></head><body>"
    "<h2>📶 WiFi Configuration</h2>"
    "<form method='POST' action='/save'>"
    "<label>WiFi Network</label><select name='ssid'>" + options + "</select>"
    "<label>Password</label><input type='password' name='pass'>"
    "<input type='submit' value='Save & Reboot'>"
    "</form><br>"
    "<form method='POST' action='/reset'><input type='submit' value='Reset WiFi Settings'></form>"
    "<p>All connections are redirected here 🧭</p></body></html>";
  server.send(200, "text/html", html);
}

void handleSave() {
  if (server.hasArg("ssid") && server.hasArg("pass")) {
    String s = server.arg("ssid"), p = server.arg("pass");
    s.toCharArray(wifiCred.ssid, 32);
    p.toCharArray(wifiCred.pass, 64);
    saveConfig();
    server.send(200, "text/html", "<p>✅ Saved! Rebooting...</p>");
    delay(2000);
    ESP.restart();
  } else {
    server.send(400, "text/plain", "Bad Request");
  }
}

void handleReset() {
  clearConfig();
  server.send(200, "text/html", "<p>⚠️ Settings cleared. Rebooting...</p>");
  delay(2000);
  ESP.restart();
}

void startConfigPortal() {
  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASS);
  dnsServer.start(DNS_PORT, "*", WiFi.softAPIP());

  server.on("/",       handleRoot);
  server.on("/save",   HTTP_POST, handleSave);
  server.on("/reset",  HTTP_POST, handleReset);
  server.onNotFound(handleRoot);
  server.begin();
  if (DEBUG_MODE)
    {Serial.println("📡 AP IP: " + WiFi.softAPIP().toString());}

  unsigned long startTime = millis();
  while (true) {
    dnsServer.processNextRequest();
    server.handleClient();

    if (millis() - startTime > CONFIG_TIMEOUT) {
      if (DEBUG_MODE)
        {Serial.println("⏰ Config timeout. Rebooting...");}
      delay(1000);
      ESP.restart();
    }
    delay(1);
  }
}

bool tryConnectSTA() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(wifiCred.ssid, wifiCred.pass);
  unsigned long start = millis();
  while (millis() - start < WIFI_RETRY_TIME) {
    if (WiFi.status() == WL_CONNECTED) return true;
    delay(500);
  }
  return false;
}

void setup() {
  Serial.begin(9600);
  megaSerial.begin(9600);
  loadConfig();

  if (wifiCred.ssid[0] == 0) {
    if (DEBUG_MODE)
      {Serial.println("🚫 No config found. Starting portal...");}
    startConfigPortal();
  } else {
    if (DEBUG_MODE)
      {Serial.printf("🔌 Connecting to %s...\n", wifiCred.ssid);}
    if (tryConnectSTA()) {
      if (DEBUG_MODE)
        {Serial.println("✅ Connected! IP = " + WiFi.localIP().toString());}
      lastConnectionCheck = millis();
    } else {
      if (DEBUG_MODE)
        {Serial.println("❌ Failed. Starting AP...");}
      startConfigPortal();
    }
  }
  config.database_url = DATABASE_URL;
  config.signer.tokens.legacy_token = DATABASE_SECRET;

  Firebase.reconnectNetwork(true);
  fbdo.setBSSLBufferSize(2048, 512);
  Firebase.begin(&config, &auth);
  configTime(7*3600, 0, "pool.ntp.org", "time.nist.gov");
  
  // Đợi đến khi NTP sync xong
  if (DEBUG_MODE)
    {Serial.print("Waiting for NTP time");}
  while (time(nullptr) < 100000) {   // nếu < 100.000 tức là chưa sync
    if (DEBUG_MODE)
      {Serial.print(".");}
    delay(500);
  }
  if (DEBUG_MODE)
    {Serial.println();}

  // Thử in ra xem giờ bây giờ
  time_t now = time(nullptr);
  struct tm tm;
  gmtime_r(&now, &tm);
  if (DEBUG_MODE)
    {Serial.printf("Current UTC: %04d-%02d-%02d %02d:%02d:%02d\n",
      tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
      tm.tm_hour, tm.tm_min, tm.tm_sec);}
}

void loop() {
  if (WiFi.status() != WL_CONNECTED && millis() - lastConnectionCheck > WIFI_RETRY_TIME) {
    if (DEBUG_MODE)
      {Serial.println("📴 WiFi lost. Re-entering config portal...");}
    startConfigPortal();
  }
  
  if (!connected) {
    Serial.println("Waiting for connection!");
    if (megaSerial.available()) {
      String initMsg = megaSerial.readStringUntil('\n');
      initMsg.trim();
      if (initMsg == "HELLO_ESP") {
        Serial.println("[ESP] Handshake start received");
        megaSerial.println("READY");
        connected = true;
      }
    }
    delay(1000);
    return; // chưa kết nối thì không đọc dữ liệu khác
  }
  if (millis() - lastSensorMillis > 2000)
  {
    lastSensorMillis = millis();
    unsigned long ts1 = time(nullptr); 
    if (ts1 == 0) ts1 = millis() / 1000;
    Firebase.setInt(fbdo, "/esp_data/working", (int)ts1);
    String serialBuffer = "";

    while (megaSerial.available()) 
    {
      char c = megaSerial.read();

      // Lọc ký tự rác (chỉ chấp nhận ASCII in được hoặc newline)
      if ((c >= 32 && c <= 126) || c == '\n' || c == '\r') {
        serialBuffer += c;
      }

      // Nếu gặp newline, xử lý gói tin
      if (c == '\n') {
        serialBuffer.trim();
        unsigned long ts = time(nullptr);  
        if (ts == 0) ts = millis() / 1000;
        Firebase.setInt(fbdo, "/esp_data/receive_serial", (int)ts);

        if (serialBuffer.startsWith("TEMP:") && serialBuffer.indexOf("HUM:") != -1) {
          // Dữ liệu hợp lệ → xử lý
          Serial.println("[ESP] Received: " + serialBuffer);

          char basePath[64];
          snprintf(basePath, sizeof(basePath), "/sensor_data/device_01/reading_%lu", ts);

          bool ok_data = Firebase.setString(fbdo,
          "/esp_data/debug/last_recv_data",
          serialBuffer.c_str()
          );
          // 2) Đẩy bool báo thành công hay không
          Firebase.setBool(fbdo,
            "/esp_data/debug/last_recv_ok",
            ok_data
          );
          // 3) Nếu lỗi thì đẩy luôn errorReason()
          if (!ok_data) {
            Firebase.setString(fbdo,
              "/esp_data/debug/last_recv_error",
              fbdo.errorReason().c_str()
            );
          }

          int tIdx = serialBuffer.indexOf("TEMP:");
          int hIdx = serialBuffer.indexOf("HUM:");
          int pIdx = serialBuffer.indexOf("PIR:");

          float temp = serialBuffer.substring(tIdx + 5, hIdx - 1).toFloat();
          float hum  = serialBuffer.substring(hIdx + 4, pIdx - 1).toFloat();
          int pir    = serialBuffer.substring(pIdx + 4).toInt();

          Firebase.setFloat(fbdo, String(basePath) + "/temperature", temp);
          Firebase.setFloat(fbdo, String(basePath) + "/humidity", hum);
          Firebase.setInt(fbdo, String(basePath) + "/pir", pir);

        } else {
          Serial.println("[ESP] Discard invalid data: " + serialBuffer);
        }

        serialBuffer = ""; // Xóa buffer sau khi xử lý
      }
    }
  }


    //   if (megaSerial.available())
    //   {
    //       unsigned long ts = time(nullptr);  
    //       if (ts == 0) ts = millis() / 1000;
    //       Firebase.setInt(fbdo, "/esp_data/receive_serial", (int)ts);
    //       char basePath[64];
    //       snprintf(basePath, sizeof(basePath), "/sensor_data/device_01/reading_%lu", ts);
          
    //       String data = megaSerial.readStringUntil('\n');
    //       data.trim();
    //       Serial.println("[ESP] Received: " + data);
    //       bool ok_data = Firebase.setString(fbdo,
    //         "/esp_data/debug/last_recv_data",
    //         data.c_str()
    //       );
    //       // 2) Đẩy bool báo thành công hay không
    //       Firebase.setBool(fbdo,
    //         "/esp_data/debug/last_recv_ok",
    //         ok_data
    //       );
    //       // 3) Nếu lỗi thì đẩy luôn errorReason()
    //       if (!ok_data) {
    //         Firebase.setString(fbdo,
    //           "/esp_data/debug/last_recv_error",
    //           fbdo.errorReason().c_str()
    //         );
    //       }
          

    //       // Dữ liệu dạng: TEMP:25.4,HUM:68,PIR:1
    //       int tIdx = data.indexOf("TEMP:");
    //       int hIdx = data.indexOf("HUM:");
    //       int pIdx = data.indexOf("PIR:");

    //       if (tIdx != -1 && hIdx != -1 && pIdx != -1)
    //       {
    //           float temp = data.substring(tIdx + 5, hIdx - 1).toFloat();
    //           float hum = data.substring(hIdx + 4, pIdx - 1).toFloat();
    //           int pir = data.substring(pIdx + 4).toInt();

    //           Firebase.setFloat(fbdo, String(basePath) + "/temperature", temp);
    //           Firebase.setFloat(fbdo, String(basePath) + "/humidity", hum);
    //           Firebase.setInt(fbdo, String(basePath) + "/pir", pir);
    //       }

    //   }
    // }

    // 2. Đọc điều khiển từ Firebase mỗi 0.2 giây
    if (millis() - lastControlMillis > 200)
    {
      lastControlMillis = millis();
      int musicControlOn = 0;
      int musicControlOff = 0;
      int ledControl = 0;
      if (Firebase.getInt(fbdo, "/controls/musicControlOn"))
        musicControlOn = fbdo.intData();
      if (Firebase.getInt(fbdo, "/controls/musicControlOff"))
        musicControlOff = fbdo.intData();
      if (Firebase.getInt(fbdo, "/controls/ledControl"))
        ledControl = fbdo.intData();
      if (ledControl)
      {
        megaSerial.println("LED:TOGGLE"); // gửi Mega
        Firebase.setInt(fbdo, "/controls/ledControl", 0);
        // if (Firebase.getString(fbdo, "/controls/led"))
        // {
        //     String ledState = fbdo.stringData();
        //     if (ledState != lastLed)
        //     {
        //         lastLed = ledState;
        //         megaSerial.println("LED:" + ledState); // gửi Mega
        //     }
        // }
      }
      
      if (musicControlOn)
      {
        if (Firebase.getInt(fbdo, "/controls/songNumber"))
        {
          int song = fbdo.intData();
          if (song >= 1 && song <= 10)
          {
              megaSerial.println("BUZZER:PLAY:" + String(song)); // gửi Mega
          }
          Firebase.setInt(fbdo, "/controls/musicControlOn", 0);
        }
      }
      if (musicControlOff)
      {
        megaSerial.println("BUZZER:STOP"); // gửi Mega
        Firebase.setInt(fbdo, "/controls/musicControlOff", 0);
      }
      
    }

  // TODO: your normal app logic here
}
