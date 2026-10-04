#include <WiFi.h>
#include <Firebase_ESP_Client.h>
#include <Wire.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>
#include <Adafruit_PN532.h>

/* WIFI */
#define WIFI_SSID "YOUR WIFI SSID"
#define WIFI_PASSWORD "YOUR WIFI PASSWORD"

/* FIREBASE */
#define API_KEY "YOUR_FIREBASE_API_KEY"
#define DATABASE_URL "YOUR_FIREBASE_DATABASE_URL"

/* TFT */
#define TFT_CS   15
#define TFT_RST  4
#define TFT_DC   2
Adafruit_ST7735 tft = Adafruit_ST7735(TFT_CS, TFT_DC, TFT_RST);

/* PN532 (I2C) */
Adafruit_PN532 nfc(21, 22);

/* RELAYS */
#define RELAY1 14
#define RELAY2 27
#define RELAY3 26
#define RELAY4 25

FirebaseData fbdo;
FirebaseData stream;
FirebaseAuth auth;
FirebaseConfig config;

/* STATES */
bool lockerOpen[5] = {false};
bool lockerDirty[5] = {false};

unsigned long lastScan = 0;
unsigned long lastWiFiCheck = 0;

/* ================= UI ================= */

void updateLockerBox(int i) {
  int y = 30 + ((i - 1) * 25);

  if (lockerOpen[i]) {
    tft.fillRect(10, y, 140, 20, ST77XX_GREEN);
    tft.setTextColor(ST77XX_BLACK);
  } else {
    tft.fillRect(10, y, 140, 20, ST77XX_RED);
    tft.setTextColor(ST77XX_WHITE);
  }

  tft.setTextSize(1);
  tft.setCursor(20, y + 6);
  tft.print("LOCKER ");
  tft.print(i);

  tft.setCursor(95, y + 6);
  tft.print(lockerOpen[i] ? "OPEN" : "CLOSED");
}

void drawUI() {
  tft.fillScreen(ST77XX_BLACK);

  tft.setTextSize(2);
  tft.setTextColor(ST77XX_WHITE);
  tft.setCursor(20, 5);
  tft.print("NEO LOCK");

  for (int i = 1; i <= 4; i++) {
    updateLockerBox(i);
  }
}

/* ================= RELAY ================= */

int getRelayPin(int locker) {
  if (locker == 1) return RELAY1;
  if (locker == 2) return RELAY2;
  if (locker == 3) return RELAY3;
  if (locker == 4) return RELAY4;
  return -1;
}

void setLockerState(int locker, String status) {
  int pin = getRelayPin(locker);
  if (pin == -1) return;

  status.trim();
  status.toUpperCase();

  Serial.println("Locker " + String(locker) + " -> " + status);

  if (status == "OPEN") {
    digitalWrite(pin, HIGH);
    lockerOpen[locker] = true;
  } else {
    digitalWrite(pin, LOW);
    lockerOpen[locker] = false;
  }

  lockerDirty[locker] = true;
}

/* ================= RFID ================= */

void handleRFID() {
  if (millis() - lastScan < 1500) return;

  uint8_t uid[7];
  uint8_t len;

  if (!nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &len, 50))
    return;

  lastScan = millis();

  String id = "";
  for (byte i = 0; i < len; i++) {
    if (uid[i] < 0x10) id += "0";
    id += String(uid[i], HEX);
  }
  id.toUpperCase();

  Serial.println("UID: " + id);

  int locker = -1;
  if (id == "837E1F11") locker = 1;
  else if (id == "938716E5") locker = 2;
  else if (id == "D6DB4006") locker = 3;
  else if (id == "A3B7DC26") locker = 4;

  if (locker == -1) {
    Serial.println("Unauthorized card");
    return;
  }

  String path = "/lockers/locker" + String(locker) + "/status";

  if (Firebase.RTDB.getString(&fbdo, path)) {

    String current = fbdo.stringData();
    current.toUpperCase();

    String newState = (current == "OPEN") ? "CLOSED" : "OPEN";

    if (Firebase.RTDB.setString(&fbdo, path, newState)) {
      Serial.println("RFID TOGGLE locker " + String(locker) + " -> " + newState);
    } else {
      Serial.println("Firebase write failed");
    }

  } else {
    Serial.println("Firebase read failed");
  }
}

/* ================= FIREBASE STREAM ================= */

void streamCallback(FirebaseStream data) {
  String path = data.dataPath();
  String value = data.stringData();

  Serial.println("PATH: " + path);
  Serial.println("VALUE: " + value);

  if (path == "/locker1/status") setLockerState(1, value);
  else if (path == "/locker2/status") setLockerState(2, value);
  else if (path == "/locker3/status") setLockerState(3, value);
  else if (path == "/locker4/status") setLockerState(4, value);
}

void streamTimeout(bool timeout) {
  if (timeout) {
    Serial.println("Stream timeout, reconnecting...");
    Firebase.RTDB.beginStream(&stream, "/lockers");
  }
}

/* ================= SETUP ================= */

void setup() {
  Serial.begin(115200);

  pinMode(RELAY1, OUTPUT);
  pinMode(RELAY2, OUTPUT);
  pinMode(RELAY3, OUTPUT);
  pinMode(RELAY4, OUTPUT);

  digitalWrite(RELAY1, LOW);
  digitalWrite(RELAY2, LOW);
  digitalWrite(RELAY3, LOW);
  digitalWrite(RELAY4, LOW);

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Connecting WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi Connected");

  config.api_key = API_KEY;
  config.database_url = DATABASE_URL;
  config.signer.test_mode = true;

  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);

  fbdo.setBSSLBufferSize(4096, 1024);
  stream.setBSSLBufferSize(4096, 1024);

  Wire.begin(21, 22);
  Wire.setClock(100000);

  nfc.begin();
  if (!nfc.getFirmwareVersion()) {
    Serial.println("PN532 NOT DETECTED");
  } else {
    Serial.println("PN532 OK");
    nfc.SAMConfig();
  }

  SPI.begin();
  tft.initR(INITR_BLACKTAB);
  tft.setRotation(3);   // ✅ ONLY CHANGE (UPSIDE DOWN)
  drawUI();

  if (!Firebase.RTDB.beginStream(&stream, "/lockers")) {
    Serial.println("Stream failed: " + stream.errorReason());
  }

  Firebase.RTDB.setStreamCallback(&stream, streamCallback, streamTimeout);

  Serial.println("SYSTEM READY");
}

/* ================= LOOP ================= */

void loop() {

  if (millis() - lastWiFiCheck > 5000) {
    lastWiFiCheck = millis();
    if (WiFi.status() != WL_CONNECTED) {
      Serial.println("Reconnecting WiFi...");
      WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    }
  }

  if (!Firebase.RTDB.readStream(&stream)) {
    if (stream.httpCode() != 200) {
      Firebase.RTDB.beginStream(&stream, "/lockers");
    }
  }

  for (int i = 1; i <= 4; i++) {
    if (lockerDirty[i]) {
      lockerDirty[i] = false;
      updateLockerBox(i);
    }
  }

  handleRFID();

  yield();
}
