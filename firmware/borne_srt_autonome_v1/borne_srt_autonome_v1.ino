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
#define ZONE_TYPE "ENTREE_RETOUR"
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
    delay(500);
    Serial.print(".");
  }

  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("Wi-Fi : connecte, IP ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("Wi-Fi : non connecte, le scan BLE continue");
    Serial.print("Wi-Fi status code : ");
    Serial.println(WiFi.status());
    Serial.println("Verifier : SSID exact, mot de passe exact, reseau 2.4 GHz, ESP32 retelverse.");
  }
}

void ensureWiFiConnected() {
  if (WiFi.status() == WL_CONNECTED) {
    return;
  }

  unsigned long now = millis();

  if (now - lastWiFiReconnectAttemptMs < WIFI_RECONNECT_INTERVAL_MS) {
    return;
  }

  lastWiFiReconnectAttemptMs = now;

  Serial.println("Wi-Fi : reconnexion automatique");
  WiFi.disconnect();
  delay(100);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
}

// ===============================
// TELEGRAM
// ===============================

String urlEncode(String value) {
  String encoded = "";
  const char* hex = "0123456789ABCDEF";

  for (int i = 0; i < value.length(); i++) {
    uint8_t c = (uint8_t)value[i];

    if (
      (c >= 'a' && c <= 'z') ||
      (c >= 'A' && c <= 'Z') ||
      (c >= '0' && c <= '9') ||
      c == '-' || c == '_' || c == '.' || c == '~'
    ) {
      encoded += (char)c;
    } else if (c == ' ') {
      encoded += "%20";
    } else if (c == '\n') {
      encoded += "%0A";
    } else {
      encoded += "%";
      encoded += hex[(c >> 4) & 0x0F];
      encoded += hex[c & 0x0F];
    }
  }

  return encoded;
}

bool sendTelegramMessage(String message) {
  Serial.println("TELEGRAM SEND START");

  if (!USE_TELEGRAM) {
    Serial.println("Telegram : desactive");
    Serial.println("TELEGRAM SEND ERROR");
    return false;
  }

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Telegram : Wi-Fi absent");
    Serial.println("TELEGRAM SEND ERROR");
    return false;
  }

  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient https;
  String url = "https://api.telegram.org/bot";
  url += TELEGRAM_BOT_TOKEN;
  url += "/sendMessage?chat_id=";
  url += urlEncode(TELEGRAM_CHAT_ID);
  url += "&text=";
  url += urlEncode(message);

  if (!https.begin(client, url)) {
    Serial.println("Telegram : echec HTTPS");
    Serial.println("TELEGRAM SEND ERROR");
    return false;
  }

  int httpCode = https.GET();
  Serial.print("TELEGRAM HTTP CODE ");
  Serial.println(httpCode);

  https.end();

  bool ok = httpCode >= 200 && httpCode < 300;
  Serial.println(ok ? "TELEGRAM SEND OK" : "TELEGRAM SEND ERROR");

  return ok;
}

// ===============================
// MESSAGES TELEGRAM
// ===============================

String getEventTitle() {
  String zone = String(ZONE_TYPE);

  if (zone == "SORTIE") {
    return "🔴 SORTIE DETECTEE";
  }

  if (zone == "PASSAGE_UNIQUE") {
    return "🟢 PASSAGE DETECTE";
  }

  return "🟢 RETOUR DETECTE";
}

String getFriendlyZoneName() {
  String zone = String(ZONE_TYPE);

  if (zone == "SORTIE") {
    return "Sortie";
  }

  if (zone == "PASSAGE_UNIQUE") {
    return "Passage unique";
  }

  return "Entree / Retour";
}

String buildTestStartupMessage() {
  String message = "";
  message += "🧪 TEST BORNE SRT";
  message += "\n";
  message += "Site : ";
  message += SITE_ID;
  message += "\n";
  message += "Borne : ";
  message += BORNE_ID;
  message += "\n";
  message += "Zone : ";
  message += ZONE_TYPE;
  message += "\n";
  message += "Wi-Fi : ";
  message += WiFi.status() == WL_CONNECTED ? "connecte" : "absent";
  message += "\n";
  message += "Telegram : ";
  message += USE_TELEGRAM ? "actif" : "desactive";
  message += "\n";
  message += "Mode : TEST";

  return message;
}

String buildPilotStartupMessage() {
  String message = "";
  message += "📡 Borne SRT en ligne";
  message += "\n";
  message += "Site : ";
  message += SITE_ID;
  message += "\n";
  message += "Borne : ";
  message += getFriendlyZoneName();
  message += "\n";
  message += "La borne ecoute les CSR Tags.";

  return message;
}

String buildStartupMessage() {
  if (String(MESSAGE_MODE) == "PILOTE") {
    return buildPilotStartupMessage();
  }

  return buildTestStartupMessage();
}

String buildTestEventMessage(int tagIndex) {
  String message = "";
  message += "🧪 TEST DETECTION SRT";
  message += "\n";
  message += "CSR Tag : ";
  message += tags[tagIndex].label;
  message += "\n";
  message += "Borne : ";
  message += BORNE_ID;
  message += "\n";
  message += "Zone : ";
  message += ZONE_TYPE;
  message += "\n";
  message += "RSSI : ";
  message += tags[tagIndex].lastRssi;
  message += "\n";
  message += "Evenement : ";
  message += getEventTitle();
  message += "\n";
  message += "Message envoye depuis la borne autonome.";

  return message;
}

String buildPilotEventMessage(int tagIndex) {
  String zone = String(ZONE_TYPE);
  String message = "";

  message += getEventTitle();
  message += "\n";
  message += "CSR Tag : ";
  message += tags[tagIndex].label;
  message += "\n";
  message += "Borne : ";
  message += getFriendlyZoneName();
  message += "\n";

  if (zone == "SORTIE") {
    message += "Le vehicule a quitte la zone de controle. Dossier SRT mis a jour.";
  } else if (zone == "PASSAGE_UNIQUE") {
    message += "Mouvement detecte dans la zone SRT.";
  } else {
    message += "Le vehicule est revenu dans la zone de controle. Dossier SRT a verifier.";
  }

  return message;
}

String buildTelegramEventMessage(int tagIndex) {
  if (String(MESSAGE_MODE) == "PILOTE") {
    return buildPilotEventMessage(tagIndex);
  }

  return buildTestEventMessage(tagIndex);
}

// ===============================
// BLE / LOGIQUE EVENEMENT
// ===============================

int findTagIndex(String mac) {
  mac.toUpperCase();

  for (int i = 0; i < TAG_COUNT; i++) {
    String known = tags[i].mac;
    known.toUpperCase();

    if (mac == known) {
      return i;
    }
  }

  return -1;
}

void validateEventIfNeeded(int tagIndex) {
  CsrTag& tag = tags[tagIndex];

  if (tag.validationCount < VALIDATION_REQUIRED_COUNT) {
    return;
  }

  unsigned long now = millis();
  bool firstEventForTag = tag.lastEventMs == 0;

  if (!tag.eventArmed) {
    Serial.print("Evenement ignore tag non rearme : ");
    Serial.println(tag.label);
    tag.validationCount = 0;
    tag.validationWindowStartMs = 0;
    return;
  }

  if (!firstEventForTag && now - tag.lastEventMs < EVENT_COOLDOWN_MS) {
    Serial.print("Evenement ignore anti-spam : ");
    Serial.println(tag.label);
    tag.validationCount = 0;
    tag.validationWindowStartMs = 0;
    return;
  }

  tag.lastEventMs = now;
  tag.validationCount = 0;
  tag.validationWindowStartMs = 0;
  tag.eventArmed = false;

  Serial.println("--------------------------------");
  Serial.println("EVENT VALIDATED");
  Serial.println("EVENEMENT SRT VALIDE");
  Serial.print("Type    : ");
  Serial.println(getEventTitle());
  Serial.print("Site    : ");
  Serial.println(SITE_ID);
  Serial.print("Borne   : ");
  Serial.println(BORNE_ID);
  Serial.print("Zone    : ");
  Serial.println(ZONE_TYPE);
  Serial.print("CSR Tag : ");
  Serial.println(tag.label);
  Serial.print("RSSI    : ");
  Serial.println(tag.lastRssi);
  Serial.println("--------------------------------");

  sendTelegramMessage(buildTelegramEventMessage(tagIndex));
}

void registerDetection(int tagIndex, int rssi) {
  CsrTag& tag = tags[tagIndex];
  unsigned long now = millis();

  tag.lastSeenMs = now;
  tag.lastRssi = rssi;

  bool windowExpired =
    tag.validationWindowStartMs == 0 ||
    now - tag.validationWindowStartMs > VALIDATION_WINDOW_MS;

  if (windowExpired) {
    tag.validationWindowStartMs = now;
    tag.validationCount = 1;
  } else {
    tag.validationCount++;
  }

  Serial.print("Detection valide : ");
  Serial.print(tag.label);
  Serial.print(" | RSSI ");
  Serial.print(rssi);
  Serial.print(" | validation ");
  Serial.print(tag.validationCount);
  Serial.print("/");
  Serial.println(VALIDATION_REQUIRED_COUNT);

  validateEventIfNeeded(tagIndex);
}

void resetTagsAfterAbsence() {
  unsigned long now = millis();

  for (int i = 0; i < TAG_COUNT; i++) {
    CsrTag& tag = tags[i];

    if (tag.eventArmed || tag.lastSeenMs == 0) {
      continue;
    }

    bool absentLongEnough = now - tag.lastSeenMs > TAG_ABSENCE_RESET_MS;
    bool cooldownFinished = now - tag.lastEventMs > EVENT_COOLDOWN_MS;

    if (absentLongEnough && cooldownFinished) {
      tag.eventArmed = true;
      tag.validationCount = 0;
      tag.validationWindowStartMs = 0;

      Serial.print("Tag rearme apres absence : ");
      Serial.println(tag.label);
    }
  }
}

class SrtAdvertisedDeviceCallbacks : public BLEAdvertisedDeviceCallbacks {
  void onResult(BLEAdvertisedDevice advertisedDevice) {
    String mac = advertisedDevice.getAddress().toString().c_str();
    mac.toUpperCase();

    int rssi = advertisedDevice.getRSSI();

    if (DEBUG_RAW_BLE) {
      Serial.print("BLE brut : ");
      Serial.print(mac);
      Serial.print(" | RSSI ");
      Serial.println(rssi);
    }

    int tagIndex = findTagIndex(mac);

    if (tagIndex < 0) {
      return;
    }

    if (rssi < RSSI_MIN) {
      Serial.print("CSR Tag ignore seuil : ");
      Serial.print(tags[tagIndex].label);
      Serial.print(" | RSSI ");
      Serial.println(rssi);
      return;
    }

    registerDetection(tagIndex, rssi);
  }
};

// ===============================
// DEMARRAGE
// ===============================

void printStartupInfo() {
  Serial.println();
  Serial.println("=======================================");
  Serial.println("BORNE SRT AUTONOME V1");
  Serial.println("=======================================");
  Serial.print("SITE_ID : ");
  Serial.println(SITE_ID);
  Serial.print("BORNE_ID : ");
  Serial.println(BORNE_ID);
  Serial.print("ZONE_TYPE : ");
  Serial.println(ZONE_TYPE);
  Serial.print("MESSAGE_MODE : ");
  Serial.println(MESSAGE_MODE);
  Serial.print("Wi-Fi : ");
  Serial.println("tentative de connexion");
  Serial.print("Telegram : ");
  Serial.println(USE_TELEGRAM ? "active" : "desactive");
  Serial.print("Nombre de CSR Tags charges : ");
  Serial.println(TAG_COUNT);
  Serial.print("RSSI_MIN : ");
  Serial.println(RSSI_MIN);
  Serial.println();
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  printStartupInfo();
  connectWiFi();

  if (WiFi.status() == WL_CONNECTED && USE_TELEGRAM) {
    startupTelegramSent = sendTelegramMessage(buildStartupMessage());
  }

  BLEDevice::init("BORNE-SRT-AUTONOME");

  pBLEScan = BLEDevice::getScan();
  pBLEScan->setAdvertisedDeviceCallbacks(new SrtAdvertisedDeviceCallbacks());
  pBLEScan->setActiveScan(true);
  pBLEScan->setInterval(100);
  pBLEScan->setWindow(99);

  Serial.println("Scan BLE pret.");
  Serial.println("La Borne SRT fonctionne en autonomie.");
}

// ===============================
// BOUCLE PRINCIPALE
// ===============================

void loop() {
  ensureWiFiConnected();

  if (!startupTelegramSent && WiFi.status() == WL_CONNECTED && USE_TELEGRAM) {
    startupTelegramSent = sendTelegramMessage(buildStartupMessage());
  }

  pBLEScan->start(BLE_SCAN_SECONDS, false);
  pBLEScan->clearResults();
  resetTagsAfterAbsence();

  delay(100);
}

