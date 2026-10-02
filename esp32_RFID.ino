#include <SPI.h>
#include <MFRC522.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include "secrets.h"  // SSID e PASSWORD — copia secrets.h.example in secrets.h

const char* BACKEND_ATTIVA    = "http://192.168.1.14/relay/0?turn=on";
const char* BACKEND_DISATTIVA = "http://192.168.1.14/relay/0?turn=off";

#define RST_PIN  22
#define SS_PIN    4
#define SCK_PIN  18
#define MOSI_PIN 23
#define MISO_PIN 19
#define TIMEOUT_DISATTIVA 1000

MFRC522 rfid(SS_PIN, RST_PIN);

String uidAttivo   = "";
bool   statoAttivo = false;
unsigned long ultimaLettura = 0;

void initClone() {
  digitalWrite(RST_PIN, LOW);  delay(50);
  digitalWrite(RST_PIN, HIGH); delay(50);
  rfid.PCD_Reset(); delay(100);
  rfid.PCD_WriteRegister(MFRC522::TModeReg,      0x80);
  rfid.PCD_WriteRegister(MFRC522::TPrescalerReg, 0xA9);
  rfid.PCD_WriteRegister(MFRC522::TReloadRegH,   0x03);
  rfid.PCD_WriteRegister(MFRC522::TReloadRegL,   0xE8);
  rfid.PCD_WriteRegister(MFRC522::TxASKReg,      0x40);
  rfid.PCD_WriteRegister(MFRC522::ModeReg,       0x3D);
  rfid.PCD_AntennaOn();
  rfid.PCD_SetAntennaGain(MFRC522::RxGain_max);
  delay(100);
}

void connectWiFi() {
  Serial.print("Connessione WiFi...");
  WiFi.begin(SSID, PASSWORD);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500); Serial.print(".");
  }
  Serial.println(" OK — IP: " + WiFi.localIP().toString());
}

void chiamaBackend(const char* url, String uid) {
  if (WiFi.status() != WL_CONNECTED) connectWiFi();
  
  Serial.print("Chiamo: ");
  Serial.println(url);
  
  HTTPClient http;
  http.begin(url);
  http.setTimeout(10000); // 10 secondi
  int code = http.GET();
  
  Serial.print("GET → "); Serial.println(code);
  if (code > 0) {
    Serial.println(http.getString());
  } else {
    Serial.print("Errore: ");
    Serial.println(http.errorToString(code));
  }
  http.end();
}

String leggiNuovoUID() {
  if (!rfid.PICC_IsNewCardPresent()) return "";
  if (!rfid.PICC_ReadCardSerial())   return "";
  String uid = "";
  for (byte i = 0; i < rfid.uid.size; i++) {
    if (rfid.uid.uidByte[i] < 0x10) uid += "0";
    uid += String(rfid.uid.uidByte[i], HEX);
    if (i < rfid.uid.size - 1) uid += ":";
  }
  uid.toUpperCase();
  rfid.PICC_HaltA();
  rfid.PCD_StopCrypto1();
  return uid;
}

bool tagAncoraPresente() {
  byte bufferATQA[2];
  byte bufferSize = sizeof(bufferATQA);
  MFRC522::StatusCode result = rfid.PICC_WakeupA(bufferATQA, &bufferSize);
  if (result == MFRC522::STATUS_OK || result == MFRC522::STATUS_COLLISION) {
    rfid.PICC_HaltA();
    return true;
  }
  return false;
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  connectWiFi();
  // Test ping Shelly
  Serial.println("\nTest raggiungibilità Shelly...");
  HTTPClient http;
  http.begin("http://192.168.1.14/status"); // endpoint info Shelly
  http.setTimeout(5000);
  int code = http.GET();
  Serial.print("Shelly /status → ");
  Serial.println(code);
  if (code > 0) {
    Serial.println(http.getString());
  } else {
    Serial.print("Errore: ");
    Serial.println(http.errorToString(code));
  }
  http.end();

  // Stampa IP ESP32
  Serial.print("IP ESP32: ");
  Serial.println(WiFi.localIP());
  pinMode(RST_PIN, OUTPUT);
  SPI.begin(SCK_PIN, MISO_PIN, MOSI_PIN, SS_PIN);
  SPI.setFrequency(1000000);
  rfid.PCD_Init();
  delay(200);
  initClone();
  Serial.println("Pronto — avvicina il tag...");
}

void loop() {
  if (!statoAttivo) {
    String uid = leggiNuovoUID();
    if (uid != "") {
      statoAttivo   = true;
      uidAttivo     = uid;
      ultimaLettura = millis();
      Serial.println("ATTIVO: " + uid);
      chiamaBackend(BACKEND_ATTIVA, uid);
    }
  } else {
    if (tagAncoraPresente()) {
      ultimaLettura = millis();
    } else if (millis() - ultimaLettura > TIMEOUT_DISATTIVA) {
      Serial.println("DISATTIVO: " + uidAttivo);
      chiamaBackend(BACKEND_DISATTIVA, uidAttivo);
      statoAttivo = false;
      uidAttivo   = "";
    }
  }
  delay(200);
}