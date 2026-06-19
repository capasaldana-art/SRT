/*
  BORNE SRT - TELEGRAM UTF8 TERRAIN - WORKFLOW OFFICIEL
  BORNE SRT - MESSAGES TELEGRAM OFFICIELS TERRAIN
  BORNE SRT - WORKFLOW COURT REACTIF VERROUILLE V1.4
  ESP32 Dev Module

  Workflow reel petit perimetre :
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

#define WIFI_SSID "A_REMPLIR"
#define WIFI_PASSWORD "A_REMPLIR"

#define TELEGRAM_BOT_TOKEN "A_REMPLIR"
#define TELEGRAM_CHAT_ID "A_REMPLIR"

#define TERMINAL_SRT_URL "https://capasaldana-art.github.io/SRT/"
#define SITE_SRT "SICT / SIXT / CSR"
#define OPERATEUR_SRT "Remi"

#define BORNE_ID "BORNE_ENTREE_01"
#define ZONE_TYPE "ENTREE_RETOUR"

#define MODE_CAPTAGE "WORKFLOW_COURT_REACTIF_VERROUILLE"
#define TELEGRAM_STYLE "TERRAIN"
#define TELEGRAM_ENABLED true
#define MOTION_TELEGRAM_ENABLED true
#define SERIAL_DEBUG_ENABLED true

#define RSSI_MIN -58
#define VALIDATION_REQUIRED_COUNT 2
#define VALIDATION_WINDOW_MS 2000
#define BLE_SCAN_SECONDS 1

#define TAG_ABSENCE_RESET_MS 7000
#define SAME_EVENT_LOCK_MS 120000
#define EVENT_COOLDOWN_MS 3000

#define MOTION_ARM_DELAY_MS 5000
#define MOTION_BASELINE_MS 3000
#define MOTION_TELEGRAM_COOLDOWN_MS 120000
#define MOTION_MAX_PER_SESSION 1

#define MOTION_USE_RSSI_FOR_TELEGRAM false
#define MOTION_USE_ADV_COUNT_FOR_TELEGRAM false
#define MOTION_USE_RAW_PAYLOAD_FOR_TELEGRAM false
#define MOTION_USE_MANUFACTURER_DATA_FOR_TELEGRAM true
#define MOTION_USE_SERVICE_DATA_FOR_TELEGRAM true

#define WIFI_RECONNECT_INTERVAL_MS 5000

// ===============================
// CSR TAGS CONNUS
// ===============================

enum TagState {
  ABSENT,
  CANDIDAT,
  PRESENT_NOTIFIE,
  ABSENCE_EN_COURS
};

struct CsrTag {
  String label;
  String mac;
  TagState state;
  unsigned long lastSeenMs;
  unsigned long candidateStartMs;
  unsigned long lastPresenceEventMs;
  int validationCount;
  int lastRssi;
  unsigned long sessionStartMs;
  unsigned long motionArmAtMs;
  bool motionBaselineDone;
  unsigned long motionBaselineStartMs;
  String baselineManufacturer;
  String baselineService;
  String lastManufacturer;
  String lastService;
  String lastPayload;
  int stableManufacturerWindows;
  int stableServiceWindows;
  unsigned long motionWindowStartMs;
  bool windowManufacturerChanged;
  bool windowServiceChanged;
  unsigned long lastMotionTelegramMs;
  int motionSentThisSession;
};

CsrTag tags[] = {
  {"CSR-01", "F0:B1:DA:66:E2:A3", ABSENT, 0, 0, 0, 0, -999, 0, 0, false, 0, "", "", "", "", "", 0, 0, 0, false, false, 0, 0},
  {"CSR-02", "C1:E8:FD:B8:94:FE", ABSENT, 0, 0, 0, 0, -999, 0, 0, false, 0, "", "", "", "", "", 0, 0, 0, false, false, 0, 0},
  {"CSR-03", "FF:C8:05:3C:65:7C", ABSENT, 0, 0, 0, 0, -999, 0, 0, false, 0, "", "", "", "", "", 0, 0, 0, false, false, 0, 0},
  {"CSR-04", "CD:FC:33:C8:8E:42", ABSENT, 0, 0, 0, 0, -999, 0, 0, false, 0, "", "", "", "", "", 0, 0, 0, false, false, 0, 0},
  {"CSR-05", "D4:39:87:CD:BA:2B", ABSENT, 0, 0, 0, 0, -999, 0, 0, false, 0, "", "", "", "", "", 0, 0, 0, false, false, 0, 0}
};

const int TAG_COUNT = sizeof(tags) / sizeof(tags[0]);

BLEScan* pBLEScan = nullptr;
unsigned long lastWiFiReconnectAttemptMs = 0;
bool startupTelegramSent = false;

// ===============================
// OUTILS
// ===============================

String stateName(TagState state) {
  if (state == ABSENT) return "ABSENT";
  if (state == CANDIDAT) return "CANDIDAT";
  if (state == PRESENT_NOTIFIE) return "PRESENT_NOTIFIE";
  return "ABSENCE_EN_COURS";
}

String detectionTimeLabel() {
  unsigned long totalSeconds = millis() / 1000;
  unsigned long hours = (totalSeconds / 3600) % 24;
  unsigned long minutes = (totalSeconds / 60) % 60;
  unsigned long seconds = totalSeconds % 60;
  char buffer[9];
  snprintf(buffer, sizeof(buffer), "%02lu:%02lu:%02lu", hours, minutes, seconds);
  return String(buffer);
}

String bytesToHex(const String& data) {
  String hex = "";
  const char* digits = "0123456789ABCDEF";
  for (int i = 0; i < data.length(); i++) {
    uint8_t b = (uint8_t)data[i];
    if (i > 0) hex += " ";
    hex += digits[(b >> 4) & 0x0F];
    hex += digits[b & 0x0F];
  }
  return hex;
}

String escapeJson(String value) {
  String escaped = "";
  for (int i = 0; i < value.length(); i++) {
    char c = value[i];
    if (c == '\"') {
      escaped += "\\\"";
    } else if (c == '\\') {
      escaped += "\\\\";
    } else if (c == '\n') {
      escaped += "\\n";
    } else if (c == '\r') {
      escaped += "\\r";
    } else if (c == '\t') {
      escaped += "\\t";
    } else {
      escaped += c;
    }
  }
  return escaped;
}

int findTagIndex(String mac) {
  mac.toUpperCase();
  for (int i = 0; i < TAG_COUNT; i++) {
    String known = tags[i].mac;
    known.toUpperCase();
    if (mac == known) return i;
  }
  return -1;
}

void debugLine(String message) {
  if (SERIAL_DEBUG_ENABLED) Serial.println(message);
}

// ===============================
// WI-FI
// ===============================

void connectWiFi() {
  Serial.println("Wi-Fi : tentative connexion");
  Serial.print("SSID utilise : ");
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
    Serial.println("Wi-Fi connecte");
    Serial.print("IP : ");
    Serial.println(WiFi.localIP());
    Serial.print("RSSI Wi-Fi : ");
    Serial.println(WiFi.RSSI());
  } else {
    Serial.println("ERREUR Wi-Fi : connexion echouee");
    Serial.print("Wi-Fi status code : ");
    Serial.println(WiFi.status());
  }
}

void ensureWiFiConnected() {
  if (WiFi.status() == WL_CONNECTED) return;
  unsigned long now = millis();
  if (now - lastWiFiReconnectAttemptMs < WIFI_RECONNECT_INTERVAL_MS) return;
  lastWiFiReconnectAttemptMs = now;
  Serial.println("Wi-Fi : reconnexion automatique");
  WiFi.disconnect();
  delay(100);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
}

// ===============================
// TELEGRAM
// ===============================

bool telegramConfigured() {
  return String(TELEGRAM_BOT_TOKEN) != "A_REMPLIR" && String(TELEGRAM_CHAT_ID) != "A_REMPLIR" &&
         String(TELEGRAM_BOT_TOKEN).length() > 0 && String(TELEGRAM_CHAT_ID).length() > 0;
}

bool telegramDebugStyle() {
  return String(TELEGRAM_STYLE) == "DEBUG";
}

bool sendTelegramMessage(String text) {
  if (!TELEGRAM_ENABLED) {
    Serial.println("Telegram : desactive");
    return false;
  }
  if (!telegramConfigured()) {
    Serial.println("Telegram : non configure");
    return false;
  }
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Telegram : impossible, Wi-Fi absent");
    return false;
  }

  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient https;

  String url = "https://api.telegram.org/bot";
  url += TELEGRAM_BOT_TOKEN;
  url += "/sendMessage";

  String body = "{";
  body += "\"chat_id\":\"";
  body += escapeJson(TELEGRAM_CHAT_ID);
  body += "\",";
  body += "\"text\":\"";
  body += escapeJson(text);
  body += "\",";
  body += "\"disable_web_page_preview\":false";
  body += "}";

  Serial.println("TELEGRAM SEND START");
  if (!https.begin(client, url)) {
    Serial.println("TELEGRAM SEND ERROR : begin HTTPS");
    return false;
  }

  https.addHeader("Content-Type", "application/json; charset=utf-8");
  int httpCode = https.POST((uint8_t*)body.c_str(), body.length());
  Serial.print("TELEGRAM HTTP CODE : ");
  Serial.println(httpCode);
  https.end();

  bool ok = httpCode >= 200 && httpCode < 300;
  Serial.println(ok ? "TELEGRAM SEND OK" : "TELEGRAM SEND ERROR");
  return ok;
}

String buildStartupMessage() {
  String msg = "";
  msg += "📡 Borne SRT prête\n\n";
  msg += "La borne est en ligne et écoute les CSR Tags.\n\n";
  msg += "Site : " + String(SITE_SRT) + "\n";
  msg += "Borne : " + String(BORNE_ID) + "\n\n";
  if (telegramDebugStyle()) {
    msg += "Zone : " + String(ZONE_TYPE) + "\n";
    msg += "Mode captage : " + String(MODE_CAPTAGE) + "\n";
    msg += "Style Telegram : DEBUG\n\n";
  }
  msg += "Terminal SRT :\n";
  msg += TERMINAL_SRT_URL;
  return msg;
}

String buildEntryMessage(int tagIndex) {
  String msg = "";
  msg += "🟢 Véhicule revenu sur parc\n\n";
  msg += "Un CSR Tag vient d'être détecté en zone entrée / retour.\n\n";
  msg += "CSR Tag : " + tags[tagIndex].label + "\n";
  msg += "Borne : " + String(BORNE_ID) + "\n";
  if (telegramDebugStyle()) {
    msg += "Zone : " + String(ZONE_TYPE) + "\n";
    msg += "Heure detection : " + detectionTimeLabel() + "\n";
    msg += "RSSI : " + String(tags[tagIndex].lastRssi) + "\n";
    msg += "Mode captage : " + String(MODE_CAPTAGE) + "\n";
  }
  msg += "\nÀ vérifier dans le Terminal SRT :\n";
  msg += TERMINAL_SRT_URL;
  return msg;
}

String buildExitMessage(int tagIndex) {
  String msg = "";
  msg += "🔴 Passage sortie détecté\n\n";
  msg += "Un CSR Tag vient d'être détecté en zone sortie.\n\n";
  msg += "CSR Tag : " + tags[tagIndex].label + "\n";
  msg += "Borne : " + String(BORNE_ID) + "\n";
  if (telegramDebugStyle()) {
    msg += "Zone : " + String(ZONE_TYPE) + "\n";
    msg += "Heure detection : " + detectionTimeLabel() + "\n";
    msg += "RSSI : " + String(tags[tagIndex].lastRssi) + "\n";
    msg += "Mode captage : " + String(MODE_CAPTAGE) + "\n";
  }
  msg += "\nVérifier le dossier avant départ :\n";
  msg += TERMINAL_SRT_URL;
  return msg;
}

String buildPresenceMessage(int tagIndex) {
  if (String(ZONE_TYPE) == "SORTIE") return buildExitMessage(tagIndex);
  return buildEntryMessage(tagIndex);
}

String buildMotionMessage(int tagIndex) {
  String msg = "";
  msg += "🟠 Mouvement à vérifier\n\n";
  msg += "Un véhicule associé à un CSR Tag semble avoir bougé.\n\n";
  msg += "CSR Tag : " + tags[tagIndex].label + "\n";
  msg += "Borne : " + String(BORNE_ID) + "\n";
  if (telegramDebugStyle()) {
    msg += "Heure detection : " + detectionTimeLabel() + "\n";
    msg += "Zone : " + String(ZONE_TYPE) + "\n";
    msg += "Mode captage : " + String(MODE_CAPTAGE) + "\n";
  }
  msg += "\nDéplacement parc, lavage ou départ possible.\n";
  msg += "On le fait ou on le laisse partir ?\n\n";
  msg += "Terminal SRT :\n";
  msg += TERMINAL_SRT_URL;
  return msg;
}

// ===============================
// PRESENCE ANTI-SPAM
// ===============================

void markSessionNotified(int tagIndex) {
  CsrTag& tag = tags[tagIndex];
  unsigned long now = millis();

  tag.state = PRESENT_NOTIFIE;
  tag.lastPresenceEventMs = now;
  tag.sessionStartMs = now;
  tag.motionArmAtMs = now + MOTION_ARM_DELAY_MS;
  tag.motionBaselineDone = false;
  tag.motionBaselineStartMs = 0;
  tag.baselineManufacturer = "";
  tag.baselineService = "";
  tag.stableManufacturerWindows = 0;
  tag.stableServiceWindows = 0;
  tag.motionWindowStartMs = 0;
  tag.windowManufacturerChanged = false;
  tag.windowServiceChanged = false;
  tag.motionSentThisSession = 0;

  Serial.println("evenement envoye : oui");
  Serial.print("heure detection : ");
  Serial.println(detectionTimeLabel());
  bool telegramOk = sendTelegramMessage(buildPresenceMessage(tagIndex));
  Serial.print("Telegram envoye : ");
  Serial.println(telegramOk ? "oui" : "non");
}

void registerPresenceDetection(int tagIndex, int rssi) {
  CsrTag& tag = tags[tagIndex];
  unsigned long now = millis();

  tag.lastSeenMs = now;
  tag.lastRssi = rssi;

  if (tag.state == PRESENT_NOTIFIE) {
    Serial.println("evenement ignore : tag deja present et notifie");
    return;
  }
  if (tag.state == ABSENCE_EN_COURS) {
    tag.state = PRESENT_NOTIFIE;
    Serial.println("evenement ignore : absence trop courte, session conservee");
    return;
  }

  if (tag.state == ABSENT) {
    tag.state = CANDIDAT;
    tag.candidateStartMs = now;
    tag.validationCount = 1;
  } else if (tag.state == CANDIDAT) {
    if (now - tag.candidateStartMs > VALIDATION_WINDOW_MS) {
      tag.candidateStartMs = now;
      tag.validationCount = 1;
    } else {
      tag.validationCount++;
    }
  }

  Serial.print("CSR Tag detecte : ");
  Serial.print(tag.label);
  Serial.print(" | RSSI ");
  Serial.print(rssi);
  Serial.print(" | validation ");
  Serial.print(tag.validationCount);
  Serial.print("/");
  Serial.print(VALIDATION_REQUIRED_COUNT);
  Serial.print(" | etat ");
  Serial.print(stateName(tag.state));
  Serial.print(" | Borne ");
  Serial.print(BORNE_ID);
  Serial.print(" | Zone ");
  Serial.print(ZONE_TYPE);
  Serial.print(" | Wi-Fi ");
  Serial.print(WiFi.status() == WL_CONNECTED ? "oui" : "non");
  Serial.print(" | heure ");
  Serial.println(detectionTimeLabel());

  bool firstEvent = tag.lastPresenceEventMs == 0;
  bool lockFinished = now - tag.lastPresenceEventMs > SAME_EVENT_LOCK_MS;

  if (tag.validationCount >= VALIDATION_REQUIRED_COUNT) {
    if (firstEvent || lockFinished) {
      markSessionNotified(tagIndex);
    } else {
      Serial.println("evenement ignore : SAME_EVENT_LOCK actif");
      tag.state = PRESENT_NOTIFIE;
    }
  }
}

void updateAbsenceStates() {
  unsigned long now = millis();
  for (int i = 0; i < TAG_COUNT; i++) {
    CsrTag& tag = tags[i];
    if (tag.lastSeenMs == 0) continue;

    if (tag.state == CANDIDAT && now - tag.lastSeenMs > VALIDATION_WINDOW_MS) {
      tag.state = ABSENT;
      tag.validationCount = 0;
      tag.candidateStartMs = 0;
      Serial.print("candidat annule : ");
      Serial.println(tag.label);
    }

    if (tag.state == PRESENT_NOTIFIE && now - tag.lastSeenMs > BLE_SCAN_SECONDS * 1500UL) {
      tag.state = ABSENCE_EN_COURS;
      Serial.print("absence en cours : ");
      Serial.println(tag.label);
    }

    if (tag.state == ABSENCE_EN_COURS && now - tag.lastSeenMs > TAG_ABSENCE_RESET_MS) {
      tag.state = ABSENT;
      tag.validationCount = 0;
      tag.candidateStartMs = 0;
      tag.motionBaselineDone = false;
      tag.motionSentThisSession = 0;
      Serial.print("tag rearme apres absence : ");
      Serial.println(tag.label);
      return;
    }
  }
}

// ===============================
// MOUVEMENT CONSERVATEUR
// ===============================

String buildPayloadSignature(BLEAdvertisedDevice advertisedDevice, String& manufacturerHex, String& serviceHex) {
  String manufacturerData = advertisedDevice.haveManufacturerData() ? advertisedDevice.getManufacturerData() : "";
  String serviceData = advertisedDevice.haveServiceData() ? advertisedDevice.getServiceData() : "";
  manufacturerHex = bytesToHex(manufacturerData);
  serviceHex = bytesToHex(serviceData);
  String signature = manufacturerHex + "|" + serviceHex;
  if (advertisedDevice.haveName()) {
    signature += "|";
    signature += advertisedDevice.getName().c_str();
  }
  return signature;
}

void observeMotion(int tagIndex, String manufacturerHex, String serviceHex, String payloadSignature) {
  CsrTag& tag = tags[tagIndex];
  unsigned long now = millis();

  if (!MOTION_TELEGRAM_ENABLED) {
    Serial.println("mouvement ignore : telegram mouvement desactive");
    tag.lastPayload = payloadSignature;
    return;
  }

  if (tag.state != PRESENT_NOTIFIE) {
    Serial.println("mouvement rejete : tag non present notifie");
    tag.lastPayload = payloadSignature;
    return;
  }
  if (now < tag.motionArmAtMs) {
    Serial.println("mouvement arme : non");
    tag.lastManufacturer = manufacturerHex;
    tag.lastService = serviceHex;
    tag.lastPayload = payloadSignature;
    return;
  }

  if (!tag.motionBaselineDone) {
    if (tag.motionBaselineStartMs == 0) {
      tag.motionBaselineStartMs = now;
      tag.baselineManufacturer = manufacturerHex;
      tag.baselineService = serviceHex;
      Serial.println("baseline mouvement : debut");
    }
    if (manufacturerHex.length() > 0) tag.baselineManufacturer = manufacturerHex;
    if (serviceHex.length() > 0) tag.baselineService = serviceHex;

    if (now - tag.motionBaselineStartMs >= MOTION_BASELINE_MS) {
      tag.motionBaselineDone = true;
      tag.motionWindowStartMs = now;
      Serial.println("baseline mouvement : terminee");
    } else {
      Serial.println("baseline mouvement : en cours");
    }
    tag.lastPayload = payloadSignature;
    return;
  }

  if (tag.motionSentThisSession >= MOTION_MAX_PER_SESSION) {
    Serial.println("mouvement rejete : deja envoye pour cette session");
    tag.lastPayload = payloadSignature;
    return;
  }
  if (now - tag.lastMotionTelegramMs < MOTION_TELEGRAM_COOLDOWN_MS) {
    Serial.println("mouvement rejete : cooldown actif");
    tag.lastPayload = payloadSignature;
    return;
  }

  bool manufacturerChanged = MOTION_USE_MANUFACTURER_DATA_FOR_TELEGRAM &&
                             manufacturerHex.length() > 0 &&
                             tag.baselineManufacturer.length() > 0 &&
                             manufacturerHex != tag.baselineManufacturer;

  bool serviceChanged = MOTION_USE_SERVICE_DATA_FOR_TELEGRAM &&
                        serviceHex.length() > 0 &&
                        tag.baselineService.length() > 0 &&
                        serviceHex != tag.baselineService;

  if (now - tag.motionWindowStartMs > MOTION_BASELINE_MS) {
    if (tag.windowManufacturerChanged) tag.stableManufacturerWindows++;
    else tag.stableManufacturerWindows = 0;

    if (tag.windowServiceChanged) tag.stableServiceWindows++;
    else tag.stableServiceWindows = 0;

    tag.motionWindowStartMs = now;
    tag.windowManufacturerChanged = false;
    tag.windowServiceChanged = false;
  }

  if (manufacturerChanged) tag.windowManufacturerChanged = true;
  if (serviceChanged) tag.windowServiceChanged = true;

  Serial.print("mouvement arme : oui | manufacturer change ");
  Serial.print(manufacturerChanged ? "oui" : "non");
  Serial.print(" | service change ");
  Serial.print(serviceChanged ? "oui" : "non");
  Serial.print(" | raw payload change ");
  Serial.println(payloadSignature != tag.lastPayload ? "oui" : "non");

  if (tag.stableManufacturerWindows >= 2 || tag.stableServiceWindows >= 2) {
    Serial.println("mouvement envoye : oui");
    tag.lastMotionTelegramMs = now;
    tag.motionSentThisSession++;
    bool telegramOk = sendTelegramMessage(buildMotionMessage(tagIndex));
    Serial.print("Telegram mouvement envoye : ");
    Serial.println(telegramOk ? "oui" : "non");
  } else {
    Serial.println("mouvement rejete : indice insuffisant");
  }

  tag.lastManufacturer = manufacturerHex;
  tag.lastService = serviceHex;
  tag.lastPayload = payloadSignature;
}

// ===============================
// CALLBACK BLE
// ===============================

class SrtAdvertisedDeviceCallbacks : public BLEAdvertisedDeviceCallbacks {
  void onResult(BLEAdvertisedDevice advertisedDevice) {
    String mac = advertisedDevice.getAddress().toString().c_str();
    mac.toUpperCase();
    int rssi = advertisedDevice.getRSSI();
    int tagIndex = findTagIndex(mac);

    if (tagIndex < 0) return;

    String manufacturerHex = "";
    String serviceHex = "";
    String payloadSignature = buildPayloadSignature(advertisedDevice, manufacturerHex, serviceHex);
    CsrTag& tag = tags[tagIndex];

    Serial.println("--------------------------------");
    Serial.print("CSR Tag detecte : ");
    Serial.println(tag.label);
    Serial.print("MAC : ");
    Serial.println(mac);
    Serial.print("RSSI : ");
    Serial.println(rssi);
    Serial.print("Borne : ");
    Serial.println(BORNE_ID);
    Serial.print("Zone : ");
    Serial.println(ZONE_TYPE);
    Serial.print("Etat : ");
    Serial.println(stateName(tag.state));
    Serial.print("Validation count : ");
    Serial.println(tag.validationCount);
    Serial.print("Wi-Fi connecte : ");
    Serial.println(WiFi.status() == WL_CONNECTED ? "oui" : "non");
    Serial.print("Heure detection : ");
    Serial.println(detectionTimeLabel());
    Serial.print("Manufacturer HEX : ");
    Serial.println(manufacturerHex.length() ? manufacturerHex : "none");
    Serial.print("Service HEX : ");
    Serial.println(serviceHex.length() ? serviceHex : "none");

    if (rssi >= RSSI_MIN) {
      registerPresenceDetection(tagIndex, rssi);
      observeMotion(tagIndex, manufacturerHex, serviceHex, payloadSignature);
    } else {
      tag.lastRssi = rssi;
      Serial.println("evenement ignore : RSSI trop faible");
    }
  }
};

// ===============================
// SETUP / LOOP
// ===============================

void printStartupInfo() {
  Serial.println();
  Serial.println("=======================================");
  Serial.println("BORNE SRT - WORKFLOW COURT REACTIF VERROUILLE V1.4");
  Serial.println("=======================================");
  Serial.print("Mode : ");
  Serial.println(MODE_CAPTAGE);
  Serial.print("Style Telegram : ");
  Serial.println(TELEGRAM_STYLE);
  Serial.print("Borne : ");
  Serial.println(BORNE_ID);
  Serial.print("Zone : ");
  Serial.println(ZONE_TYPE);
  Serial.print("SSID Wi-Fi utilise : ");
  Serial.println(WIFI_SSID);
  Serial.print("Telegram configure : ");
  Serial.println(telegramConfigured() ? "oui" : "non");
  Serial.print("RSSI_MIN : ");
  Serial.println(RSSI_MIN);
  Serial.print("CSR Tags charges : ");
  Serial.println(TAG_COUNT);
  Serial.println("Mouvement : conserve, anti-spam actif.");
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  printStartupInfo();
  connectWiFi();

  if (WiFi.status() == WL_CONNECTED) {
    startupTelegramSent = sendTelegramMessage(buildStartupMessage());
  }

  BLEDevice::init("BORNE-SRT-ANTISPAM");
  pBLEScan = BLEDevice::getScan();
  pBLEScan->setAdvertisedDeviceCallbacks(new SrtAdvertisedDeviceCallbacks());
  pBLEScan->setActiveScan(true);
  pBLEScan->setInterval(80);
  pBLEScan->setWindow(70);

  Serial.println("Scan BLE pret - workflow court reactif verrouille.");
}

void loop() {
  ensureWiFiConnected();

  if (!startupTelegramSent && WiFi.status() == WL_CONNECTED) {
    startupTelegramSent = sendTelegramMessage(buildStartupMessage());
  }

  pBLEScan->start(BLE_SCAN_SECONDS, false);
  pBLEScan->clearResults();
  updateAbsenceStates();
  delay(50);
}
