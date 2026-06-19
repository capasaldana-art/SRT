/*
  BORNE SRT AUTONOME V1
  ESP32 Dev Module

  Flux terrain :
  CSR Tag BLE reel -> ESP32 -> Wi-Fi -> Telegram direct

  Aucun PC requis.
  Aucun bridge serie.
  Aucun simulateur.
*/

#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>

#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>

// ===============================
// CONFIGURATION A REMPLIR
// ===============================

#define SITE_ID "PILOTE_SICT"
#define BORNE_ID "BORNE_ENTREE_01"
#define ZONE_TYPE "ENTRE"
#define MESSAGE_MODE "TEST"

#define WIFI_SSID "A_REMPLIR"
#define WIFI_PASSWORD "A_REMPLIR"

#define TELEGRAM_BOT_TOKEN "A_REMPLIR"
#define TELEGRAM_CHAT_ID "A_REMPLIR"

#define USE_TELEGRAM true
#define DEBUG_RAW_BLE false

#define RSSI_MIN -75
#define VALIDATION_REQUIRED_COUNT 2
#define VALIDATION_WINDOW_MS 5000
#define EVENT_COOLDOWN_MS 30000
#define TAG_ABSENCE_RESET_MS 30000
#define BLE_SCAN_SECONDS 2
#define WIFI_RECONNECT_INTERVAL_MS 5000

// ===============================
// CSR TAGS CONNUS
// ===============================

struct CsrTag {
  String label;
  String mac;
  unsigned long lastSeenMs;
  int validationCount;
  unsigned long validationWindowStartMs;
  unsigned long lastEventMs;
  int lastRssi;
  bool eventArmed;
};

CsrTag tags[] = {
  {"CSR-01", "F0:B1:DA:66:E2:A3", 0, 0, 0, 0, -999, true},
  {"CSR-02", "C1:E8:FD:B8:94:FE", 0, 0, 0, 0, -999, true},
  {"CSR-03", "FF:C8:05:3C:65:7C", 0, 0, 0, 0, -999, true},
  {"CSR-04", "CD:FC:33:C8:8E:42", 0, 0, 0, 0, -999, true},
  {"CSR-05", "D4:39:87:CD:BA:2B", 0, 0, 0, 0, -999, true}
};

const int TAG_COUNT = sizeof(tags) / sizeof(tags[0]);

BLEScan* pBLEScan = nullptr;
unsigned long lastWiFiReconnectAttemptMs = 0;
bool startupTelegramSent = false;

// ===============================
// WI-FI
// ===============================

void connectWiFi() {
  Serial.println("Wi-Fi : tentative de connexion");

  if (String(WIFI_SSID) == "A_REMPLIR") {
    Serial.println("ERREUR CONFIG WIFI : WIFI_SSID vaut encore A_REMPLIR.");
    Serial.println("Ouvre le fichier .ino, remplis WIFI_SSID en haut, puis retelverse sur l'ESP32.");
    return;
  }

  Serial.print("Wi-Fi SSID configure : ");
  Serial.println(WIFI_SSID);

  WiFi.mode(WIFI_STA);
  WiFi.disconnect(true);
  delay(300);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  unsigned long startMs = millis();

  while (WiFi.status() != WL_CONNECTED && millis() - startMs < 15000) {
