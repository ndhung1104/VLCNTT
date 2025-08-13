#include "SPI.h"
#include "TFT_22_ILI9225.h"
#include <DHT.h>

#define TFT_RST 47
#define TFT_RS  45
#define TFT_CS  53 // SS
#define TFT_SDI 51 // MOSI
#define TFT_CLK 52 // SCK
#define TFT_LED 0   // 0 if wired to +5V directly
#define DHT11_PIN 2
#define BUZZER_PIN 12
#define LED_PIN 13
#define DHTTYPE DHT11
#define PIR_PIN 14
bool espReady = false;
const int potPin = A0;
const int buttonPin = 6;
// Cấu hình chân LCD

#define TFT_BRIGHTNESS 200 // Initial brightness of TFT backlight (optional)
TFT_22_ILI9225 tft = TFT_22_ILI9225(TFT_RST, TFT_RS, TFT_CS, TFT_LED, TFT_BRIGHTNESS);

// DHT
DHT dht(DHT11_PIN, DHTTYPE);
float temp = 0;
float humi = 0;

//MENU
int lastPotValue = -1;
int lastButtonState = HIGH;
unsigned long lastDebounceTime = 0;
unsigned long debounceDelay = 50;

int menuLevel = 0;
int currentOption = 0;
bool buttonPressed = false;
bool pirPin = false;
int selectedSong = 0;

bool inStatusView = false;

// Nhac
const char* songs[] = {"Bai 1", "Bai 2", "Bai 3"};
const int melody1[] = {262, 294, 330, 349};
const int melody2[] = {330, 294, 262, 330};
const int melody3[] = {392, 370, 349, 330};

const int *melodies[] = {melody1, melody2, melody3};
const int melodyLengths[] = {4, 4, 4};
bool isPlaying = false;
unsigned long noteStartTime = 0;
int currentNote = 0;
int noteDuration = 300; // Thời lượng mỗi nốt


// ——— thêm biến cho giao tiếp và remote control ———
bool remoteEnabled      = false;             // cờ bật/tắt nhận lệnh từ ESP
unsigned long lastComm  = 0;
unsigned long lastSend  = 0;
const unsigned long commInterval = 200;      // check mỗi 200ms
const unsigned long sendInterval = 5000;     // gửi data mỗi 5s
// Số menu phụ
enum MenuLevels {
  MAIN_MENU = 0,
  MUSIC_MENU = 1,
  MUSIC_SELECT = 2,
  LED_MENU = 3,
  STATUS_MENU = 4
};



void setup() {
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, HIGH);
  dht.begin();
  Serial.begin(9600);
  Serial1.begin(9600);
  pinMode(10, OUTPUT);
  digitalWrite(10, HIGH); // LED bắt đầu tắt
  pinMode(buttonPin, INPUT_PULLUP);
  pinMode(PIR_PIN, INPUT);
  tft.begin();
  // // tft.drawRectangle(0, 0, tft.maxX() - 1, tft.maxY() - 1, COLOR_WHITE);
  // tft.setFont(Terminal6x8);
  // tft.drawText(10, 10, "hello!");
  tft.setOrientation(0);           // Rất quan trọng để định hướng chuẩn
  tft.setFont(Terminal6x8);        // Bắt buộc để vẽ text
  tft.setBackgroundColor(COLOR_WHITE);
  
  displayMenu();

  while (!espReady) {
    Serial1.println("HELLO_ESP");
    Serial.println("[Mega] Waiting for READY...");
    delay(500);

    if (Serial1.available()) {
      String msg = Serial1.readStringUntil('\n');
      msg.trim();
      if (msg == "READY") {
        espReady = true;
        Serial.println("[Mega] ESP is ready!");
      }
    }
  }

  Serial.println("Setup completed!");
}

void readDHT()
{
  temp = dht.readTemperature();
  humi = dht.readHumidity();
}

void startPlayingSong() {
  isPlaying = true;
  currentNote = 0;
  noteStartTime = millis();
  tone(BUZZER_PIN, melodies[selectedSong][currentNote], noteDuration);
}
void updateSongPlayback() {
  if (!isPlaying) return;

  if (millis() - noteStartTime >= noteDuration + 50) { // 50ms nghỉ
    currentNote++;
    if (currentNote >= melodyLengths[selectedSong]) {
      noTone(BUZZER_PIN);
      digitalWrite(BUZZER_PIN, HIGH);
      isPlaying = false;
    } else {
      tone(BUZZER_PIN, melodies[selectedSong][currentNote], noteDuration);
      noteStartTime = millis();
    }
  }
}

void handleRemoteComm(float temp, float humi){
  unsigned long now = millis();
  if(now - lastComm < commInterval) return;
  lastComm = now;

  // 1) Gửi dữ liệu
  // Serial1.printf("TEMP:%.1f,HUM:%.1f\n", temp, humi);
  if (now - lastSend > sendInterval)
  {
    Serial1.print("TEMP:");
    Serial1.print(temp, 1);    // 1 chữ số thập phân
    Serial1.print(",HUM:");
    Serial1.print(humi, 0);  // println sẽ thêm '\n'
    Serial1.println(",PIR:1");
    lastSend = now;
  }
  
  // ESP_Serial.println("TEMP:31.1,HUM:40,PIR:1");
  // ESP_Serial.println(temp, 1);
  // Serial.print("TEMP:");
  // Serial.print(temp, 1);    // 1 chữ số thập phân
  // Serial.print(",HUM:");
  // Serial.print(humi, 0);  // println sẽ thêm '\n'
  // Serial.println(",PIR:1");
  // Serial.println("TEMP:31.1,HUM:40,PIR:1");
  // 2) Nhận lệnh (nếu chế độ remote bật)
  while(Serial1.available()){
    String cmd = Serial1.readStringUntil('\n');
    Serial.println(cmd);
    cmd.trim();
    if(!remoteEnabled) continue;

    if(cmd == "LED:TOGGLE") {
      digitalWrite(LED_PIN, HIGH);
      delay(200);
      digitalWrite(LED_PIN, LOW);
    }
    else if(cmd.startsWith("BUZZER:PLAY:")){
      int id = cmd.substring(12).toInt();
      if(id >= 0 && id < sizeof(melodies)/sizeof(melodies[0])){
        selectedSong = id;
        startPlayingSong();
      }
    }
    else if (cmd == "BUZZER:STOP")
    {
      noTone(BUZZER_PIN);
      digitalWrite(BUZZER_PIN, HIGH);
    }
  }
}

void loop() {
  readDHT();
  readPot();
  checkButton();
  updateSongPlayback();
  handleRemoteComm(temp, humi);
  delay(10);
}

void readPot() {
  int potValue = analogRead(potPin);
  int maxItem = getMenuItemCount();
  int segment = 1024 / maxItem;
  int option = potValue / segment;
  if (option >= maxItem) option = maxItem - 1;

  if (option != currentOption) {
    currentOption = option;
    displayMenu();
  }
}

void checkButton() {
  int reading = digitalRead(buttonPin);

  if (reading != lastButtonState) {
    lastDebounceTime = millis();
  }

  if ((millis() - lastDebounceTime) > debounceDelay) {
    if (reading == LOW && !buttonPressed) {
      buttonPressed = true;
      handleSelection();
    } else if (reading == HIGH) {
      buttonPressed = false;
    }
  }

  lastButtonState = reading;
}

int getMenuItemCount() {
  switch (menuLevel) {
    case MAIN_MENU: return 4;
    case MUSIC_MENU: return 4;
    case MUSIC_SELECT: return sizeof(songs) / sizeof(songs[0]);
    case LED_MENU: return 2; // Đổi trạng thái, Quay lại
    case STATUS_MENU: return 1;
    default: return 0;
  }
}

// void displayMenu() {
//   Serial.println("\n--- MENU ---");

//   switch (menuLevel) {
//     case MAIN_MENU:
//       Serial.println(currentOption == 0 ? "> Trang thai" : "  Trang thai");
//       Serial.println(currentOption == 1 ? "> Chinh led" : "  Chinh led");
//       Serial.println(currentOption == 2 ? "> Chinh nhac" : "  Chinh nhac");
//       break;

//     case MUSIC_MENU:
//       Serial.println(currentOption == 0 ? "> Chon nhac" : "  Chon nhac");
//       Serial.println(currentOption == 1 ? "> Phat nhac" : "  Phat nhac");
//       Serial.println(currentOption == 2 ? "> Dung phat" : "  Dung phat");
//       Serial.println(currentOption == 3 ? "> Quay lai" : "  Quay lai");
//       break;

//     case MUSIC_SELECT:
//       Serial.println("Danh sach bai hat:");
//       for (int i = 0; i < sizeof(songs) / sizeof(songs[0]); i++) {
//         Serial.print(currentOption == i ? "> " : "  ");
//         Serial.println(songs[i]);
//       }
//       break;

//     case LED_MENU:
//       Serial.println(currentOption == 0 ? "> Bat LED" : "  Bat LED");
//       Serial.println(currentOption == 1 ? "> Tat LED" : "  Tat LED");
//       Serial.println(currentOption == 2 ? "> Quay lai" : "  Quay lai");
//       break;

//     case STATUS_MENU:
//       Serial.println("Trang thai: Tat ca binh thuong.");
//       Serial.println("Bam nut de quay lai.");
//       break;
//   }
// }

void displayMenu() {
  // Xóa màn hình LCD
  // tft.fillScreen(COLOR_BLACK);
  tft.setFont(Terminal6x8);
  tft.setBackgroundColor(COLOR_BLACK);
  tft.clear();
  Serial.println("\n--- MENU ---");

  int y = 10; // Vị trí bắt đầu in trên LCD

  switch (menuLevel) {
    case MAIN_MENU: {
      const char* items[] = {"Trang thai", "Chinh led", "Chinh nhac", remoteEnabled ? "Remote: ON" : "Remote: OFF"};
      for (int i = 0; i < 4; i++) {
        bool selected = (i == currentOption);
        Serial.println(selected ? String("> ") + items[i] : String("  ") + items[i]);
        
        tft.drawText(10, y, items[i], selected ? COLOR_YELLOW : COLOR_WHITE);
        y += 10;
      }
      break;
    }

    case MUSIC_MENU: {
      const char* items[] = {"Chon nhac", "Phat nhac", "Dung phat", "Quay lai"};
      for (int i = 0; i < 4; i++) {
        bool selected = (i == currentOption);
        Serial.println(selected ? String("> ") + items[i] : String("  ") + items[i]);
        tft.drawText(10, y, items[i], selected ? COLOR_YELLOW : COLOR_WHITE);
        y += 10;
      }
      break;
    }

    case MUSIC_SELECT: {
      Serial.println("Danh sach bai hat:");
      tft.drawText(10, y, "Danh sach bai hat:", COLOR_WHITE);
      y += 10;

      for (int i = 0; i < sizeof(songs) / sizeof(songs[0]); i++) {
        bool selected = (i == currentOption);
        Serial.println(selected ? String("> ") + songs[i] : String("  ") + songs[i]);
        tft.drawText(10, y, songs[i], selected ? COLOR_YELLOW : COLOR_WHITE);
        y += 10;
      }
      break;
    }

    case LED_MENU: {
      const char* items[] = {"Doi trang thai LED", "Quay lai"};
      for (int i = 0; i < 2; i++) {
        bool selected = (i == currentOption);
        Serial.println(selected ? String("> ") + items[i] : String("  ") + items[i]);
        tft.drawText(10, y, items[i], selected ? COLOR_YELLOW : COLOR_WHITE);
        y += 10;
      }
      break;
    }

    case STATUS_MENU: {
      // const char* msg = "Trang thai: Tat ca binh thuong.";
      // Serial.println(msg);
      // Serial.println("Bam nut de quay lai.");
      // tft.drawText(10, y, msg, COLOR_WHITE);
      // y += 10;
      // tft.drawText(10, y, "Bam nut de quay lai.", COLOR_WHITE);
      // break;

      Serial.print("Nhiet do: ");
      Serial.println(temp);
      Serial.print("Do am: ");
      Serial.println(humi);
      Serial.println("Bam nut de quay lai.");

      char buffer[32];
      char tempStr[8];
      char humiStr[8];

      dtostrf(temp, 4, 1, tempStr); // 4 ký tự rộng, 1 số sau dấu chấm
      dtostrf(humi, 4, 1, humiStr);

      snprintf(buffer, sizeof(buffer), "Nhiet do: %s C", tempStr);
      tft.drawText(10, y, buffer, COLOR_WHITE);
      y += 12;

      snprintf(buffer, sizeof(buffer), "Do am: %s %%", humiStr);
      tft.drawText(10, y, buffer, COLOR_WHITE);
      y += 12;

      tft.drawText(10, y, "Bam nut de quay lai.", COLOR_WHITE);
      break;
    }
  }
}


void handleSelection() {
  switch (menuLevel) {
    case MAIN_MENU:
      if (currentOption == 0) {
        menuLevel = STATUS_MENU;
        inStatusView = true;
        displayMenu();
      } else if (currentOption == 1) {
        menuLevel = LED_MENU;
        // currentOption = 0;
        displayMenu();
      } else if (currentOption == 2) {
        menuLevel = MUSIC_MENU;
        // currentOption = 0;
        displayMenu();
      } else if(currentOption == 3){
        remoteEnabled = !remoteEnabled; // bật/tắt remote
        Serial.println("Remote Enabled = " + String(remoteEnabled));
        displayMenu();
      // ở lại MAIN_MENU
      }
      break;

    case STATUS_MENU:
      if (inStatusView) {
        inStatusView = false;
        menuLevel = MAIN_MENU;
        // currentOption = 0;
        displayMenu();
      }
      break;

    case LED_MENU:
      if (currentOption == 0) {
        digitalWrite(LED_PIN, HIGH);
        delay(200);
        digitalWrite(LED_PIN, LOW);
        Serial.println("Da toggle LED.");
      } else if (currentOption == 1) {
        menuLevel = MAIN_MENU;
        // currentOption = 0;
        displayMenu();
        // Serial.println("Da tat LED.");
        // digitalWrite(10, HIGH);
      } // else if (currentOption == 2) {
        // menuLevel = MAIN_MENU;
        // currentOption = 0;
        // displayMenu();
      // }
      break;

    case MUSIC_MENU:
      if (currentOption == 0) {
        menuLevel = MUSIC_SELECT;
        currentOption = 0;
        displayMenu();
      } else if (currentOption == 1) {
        Serial.print("Dang phat: ");
        Serial.println(songs[selectedSong]);
        startPlayingSong();
      } else if (currentOption == 2) {
        Serial.println("Da dung phat nhac.");
        noTone(BUZZER_PIN);
        digitalWrite(BUZZER_PIN, HIGH);
      } else if (currentOption == 3) {
        menuLevel = MAIN_MENU;
        currentOption = 0;
        displayMenu();
      }
      break;

    case MUSIC_SELECT:
      selectedSong = currentOption;
      Serial.print("Da chon: ");
      Serial.println(songs[selectedSong]);
      menuLevel = MUSIC_MENU;
      currentOption = 0;
      displayMenu();
      break;
  }
}
