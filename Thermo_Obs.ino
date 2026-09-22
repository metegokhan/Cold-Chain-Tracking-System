#include <Arduino.h>
#include <U8g2lib.h>
#include <Wire.h>
#include <WiFi.h>
#include <esp_wps.h>
#include <HTTPClient.h>
#include <NetworkClientSecure.h>
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEScan.h>
#include <LittleFS.h>
#include <time.h>
#include <qrcode.h>
#include <esp_bt.h>
#include "history_manager.h"
#include "config_manager.h"
#include "web_portal.h"

// --- Global Objects ---
ConfigManager cfgMgr;
WebPortal portal;
std::vector<DiscoveredBLEDevice> discoveredBLEs;
BLEScan* pBLEScan = nullptr;

// OLED Display (C3 Mini 0.42" OLED - X:28, Y:24)
U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, /* reset=*/ U8X8_PIN_NONE, /* clock=*/ 6, /* data=*/ 5);
const int X_OFFSET = 28; 
const int Y_OFFSET = 24; 
const int SCREEN_W = 72;
const int SCREEN_H = 40;

// Hardware Pin Definition (Boot Button)
const int BOOT_BTN_PIN = 9;

// Hardware Interrupt for Instant Button Press Tracking
volatile unsigned long btnPressStart = 0;
volatile bool btnIsPressed = false;

void IRAM_ATTR isrButtonChange() {
  bool state = (digitalRead(BOOT_BTN_PIN) == LOW);
  if (state) {
    btnPressStart = millis();
    btnIsPressed = true;
  } else {
    btnIsPressed = false;
  }
}

// Operating Modes
enum SystemMode {
  MODE_NORMAL_RUN,
  MODE_MENU,
  MODE_WPS,
  MODE_WIFI_PORTAL,
  MODE_MANUAL_BLE_DISCOVERY
};

SystemMode sysMode = MODE_NORMAL_RUN;

// Menu Variables
int menuSelection = 0; // 0: WPS, 1: WiFiManager, 2: BLE Discovery
unsigned long menuLastActionTime = 0;

// Normal Mode Info Screens (Manual Navigation via Button)
enum InfoScreen {
  SCR_MAIN_TEMP = 0, // 0: Main Temperature Screen (+ w:V/X b:V/X)
  SCR_HUMIDITY,      // 1: Humidity Screen
  SCR_BATTERY,       // 2: Battery Screen (% and Voltage)
  SCR_RSSI,          // 3: BLE Signal Strength (dBm)
  SCR_MIN_TEMP,      // 4: 30d Minimum Temp & Duration
  SCR_MAX_TEMP,      // 5: 30d Maximum Temp & Duration
  SCR_WIFI_INFO,     // 6: Wi-Fi Status & IP Address
  SCR_BLE_INFO,      // 7: BLE Device Name & MAC Address
  SCR_TOTAL_COUNT
};

InfoScreen currentInfoScr = SCR_MAIN_TEMP;
unsigned long lastScreenSwitchTime = 0;
bool isBrowsingScreens = false; // True while user is actively browsing screens

// State Machine
enum NormalAppState {
  STATE_SCAN_BLE,
  STATE_SEND_WIFI,
  STATE_WAIT_INTERVAL
};
NormalAppState appState = STATE_SCAN_BLE;

unsigned long stateStageStartTime = 0;
unsigned long waitIntervalStartTime = 0;

// Sensor & Telemetry Data
volatile bool hasFreshData = false;
float measuredTemp = 0.0;
float measuredHum = 0.0;
int measuredBattery = 0;
float measuredVoltage = 0.0;
int measuredRssi = -999;
String measuredDeviceName = "ATC_......";
String measuredMacAddress = "--:--:--:--:--:--";

float lastDispTemp = 0.0;
float lastDispHum = 0.0;
int lastDispBattery = 0;
float lastDispVoltage = 0.0;
int lastDispRssi = -999;
bool everReceivedAnyData = false;
unsigned long lastBlePacketReceivedTime = 0;

// Connectivity Flags
bool wifiConnectedStatus = false;
bool bleConnectedStatus = false;
bool lastWiFiSuccess = false;
String lastAssignedIp = "-";

// 30 Days Min/Max History Buffer (8640 samples: 30 days * 24 hours * 12 samples/hour @ 5 min intervals)
TempRecord tempHistory[HISTORY_SIZE];
int historyHead = 0;
int historyCount = 0;
unsigned long lastHistorySampleTime = 0;
unsigned long lastHistorySaveTime = 0;
const char* HISTORY_FILE_PATH = "/history.bin";

// NTP Time Synchronization Engine
bool isTimeSynced = false;
bool ntpFailedWarning = false;
unsigned long lastNtpSyncTime = 0;
unsigned long lastNtpErrorAlertTime = 0;

// Telegram Multi-Event Alert State Machine
bool wasPowerOutage = false;
bool hasInitializedPowerState = false;
bool wasBleConnected = false;
bool hasInitializedBleState = false;
enum TempAlarmState { STATE_TEMP_NORMAL, STATE_TEMP_ALARM_LOW, STATE_TEMP_ALARM_HIGH };
TempAlarmState currentTempAlarmState = STATE_TEMP_NORMAL;

float lastSlopeTemp = -999.0;
unsigned long lastRapidSlopeAlertTime = 0;

// Telegram Inbound Bot State
long lastTelegramUpdateId = 0;

void syncNtpTime() {
  Serial.println("[NTP] Syncing network time (GMT+3)...");
  // GMT+3 (Turkey / Europe/Istanbul) = 3 * 3600 = 10800s offset, 0 daylight saving
  configTime(3 * 3600, 0, "pool.ntp.org", "time.google.com", "time.cloudflare.com");
  struct tm timeinfo;
  if (getLocalTime(&timeinfo, 6000)) {
    isTimeSynced = true;
    ntpFailedWarning = false;
    lastNtpSyncTime = millis();
    Serial.printf("[NTP] Time Synced Successfully: %02d.%02d.%04d %02d:%02d:%02d\n",
                  timeinfo.tm_mday, timeinfo.tm_mon + 1, timeinfo.tm_year + 1900,
                  timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
  } else {
    isTimeSynced = false;
    ntpFailedWarning = true;
    Serial.println("[NTP] ⚠️ Time Sync Failed or timed out.");
  }
}

void saveHistoryToFS() {
  File f = LittleFS.open(HISTORY_FILE_PATH, "w");
  if (!f) {
    Serial.println("[LittleFS] Failed to open history file for writing!");
    return;
  }
  f.write((uint8_t*)&historyHead, sizeof(historyHead));
  f.write((uint8_t*)&historyCount, sizeof(historyCount));
  f.write((uint8_t*)tempHistory, sizeof(tempHistory));
  f.close();
  Serial.printf("[LittleFS] Successfully saved history to flash (%d samples, %u bytes)\n", historyCount, (unsigned int)sizeof(tempHistory));
}

void loadHistoryFromFS() {
  if (!LittleFS.exists(HISTORY_FILE_PATH)) {
    Serial.println("[LittleFS] No existing history file found. Starting fresh.");
    return;
  }
  File f = LittleFS.open(HISTORY_FILE_PATH, "r");
  if (!f) {
    Serial.println("[LittleFS] Failed to open history file for reading!");
    return;
  }
  if (f.size() != (sizeof(historyHead) + sizeof(historyCount) + sizeof(tempHistory))) {
    Serial.println("[LittleFS] History file size mismatch or structure changed, resetting history.");
    f.close();
    LittleFS.remove(HISTORY_FILE_PATH);
    return;
  }
  f.read((uint8_t*)&historyHead, sizeof(historyHead));
  f.read((uint8_t*)&historyCount, sizeof(historyCount));
  f.read((uint8_t*)tempHistory, sizeof(tempHistory));
  f.close();
  if (historyHead < 0 || historyHead >= HISTORY_SIZE) historyHead = 0;
  if (historyCount < 0 || historyCount > HISTORY_SIZE) historyCount = 0;
  Serial.printf("[LittleFS] Loaded history from flash: %d samples restored.\n", historyCount);
}

void addTempSample(float t) {
  time_t nowSec = time(nullptr);
  uint32_t currentEpoch = (uint32_t)nowSec;
  // If time is not synced yet, record epoch as 0
  if (nowSec < 1000000000) currentEpoch = 0;

  int16_t val = (int16_t)(round(t * 10.0));
  tempHistory[historyHead].timestamp = currentEpoch;
  tempHistory[historyHead].temp = val;
  historyHead = (historyHead + 1) % HISTORY_SIZE;
  if (historyCount < HISTORY_SIZE) historyCount++;
}

void get30dMinMax(float &outMin, int &outMinDurationHours, float &outMax, int &outMaxDurationHours) {
  if (historyCount == 0) {
    outMin = 0.0; outMinDurationHours = 0;
    outMax = 0.0; outMaxDurationHours = 0;
    return;
  }

  int16_t minVal = 30000;
  int16_t maxVal = -30000;

  for (int i = 0; i < historyCount; i++) {
    int16_t v = tempHistory[i].temp;
    if (v < minVal) minVal = v;
    if (v > maxVal) maxVal = v;
  }

  outMin = minVal / 10.0;
  outMax = maxVal / 10.0;

  int minBandLow = minVal - 5;
  int minBandHigh = minVal + 5;
  int maxBandLow = maxVal - 5;
  int maxBandHigh = maxVal + 5;

  int minDurSamples = 0;
  int maxDurSamples = 0;

  for (int i = 0; i < historyCount; i++) {
    int16_t v = tempHistory[i].temp;
    if (v >= minBandLow && v <= minBandHigh) minDurSamples++;
    if (v >= maxBandLow && v <= maxBandHigh) maxDurSamples++;
  }

  // Each sample is 5 minutes -> total minutes = minDurSamples * 5 -> convert to hours:
  outMinDurationHours = (minDurSamples * 5 + 30) / 60;
  outMaxDurationHours = (maxDurSamples * 5 + 30) / 60;
}

// Power Status
bool isPowerOutage = false;
unsigned long lastPowerAlertTime = 0;
unsigned long lastLimitAlertTime = 0;

// Discovery
int discoveryBestRssi = -999;
String candidateMac = "";
String candidateName = "";
float candidateTemp = 0.0;
float candidateHum = 0.0;
int candidateBattery = 0;
float candidateVoltage = 0.0;
bool foundCandidate = false;

// WPS
static esp_wps_config_t wps_config;
bool wpsSuccess = false;

// Function Prototypes
void drawNormalScreens();
void handleButtonState();
bool checkMenuAbort();
void stopBLE();
void stopBLEAndFreeMem();
void startBLEScanForMode();
bool sendTelegramReply(const String& chatId, const String& msg);
void handleTelegramBotCommand(const String& chatId, String cmd);
void processTelegramIncomingMessages();

String urlEncode(String str) {
  String encoded = "";
  char c;
  char code0;
  char code1;
  for (unsigned int i = 0; i < str.length(); i++) {
    c = str.charAt(i);
    if (c == ' ') {
      encoded += '+';
    } else if (isalnum(c)) {
      encoded += c;
    } else {
      code1 = (c & 0xf) + '0';
      if ((c & 0xf) > 9) {
        code1 = (c & 0xf) - 10 + 'A';
      }
      c = (c >> 4) & 0xf;
      code0 = c + '0';
      if (c > 9) {
        code0 = c - 10 + 'A';
      }
      encoded += '%';
      encoded += code0;
      encoded += code1;
    }
  }
  return encoded;
}

// BTHome V2 Parser
bool parseBTHome(const uint8_t* data, size_t len, float &outT, float &outH, int &outB, float &outV) {
  if (len < 3) return false;
  size_t i = 1;
  bool parsedAny = false;

  while (i < len) {
    uint8_t objId = data[i++];
    if (i >= len) break;

    if (objId == 0x00) {
      i += 1;
    } else if (objId == 0x01) {
      outB = data[i++];
      parsedAny = true;
    } else if (objId == 0x02) {
      if (i + 1 < len) {
        int16_t rawTemp = (int16_t)(data[i] | (data[i + 1] << 8));
        outT = rawTemp * 0.01;
        i += 2;
        parsedAny = true;
      }
    } else if (objId == 0x03) {
      if (i + 1 < len) {
        uint16_t rawHum = (uint16_t)(data[i] | (data[i + 1] << 8));
        outH = rawHum * 0.01;
        i += 2;
        parsedAny = true;
      }
    } else if (objId == 0x0C) {
      if (i + 1 < len) {
        uint16_t rawVolt = (uint16_t)(data[i] | (data[i + 1] << 8));
        outV = rawVolt * 0.001;
        i += 2;
      }
    } else {
      i++;
    }
  }
  return parsedAny;
}

class MyAdvertisedDeviceCallbacks: public BLEAdvertisedDeviceCallbacks {
    void onResult(BLEAdvertisedDevice advertisedDevice) {
      BLEUUID bthomeUUID((uint16_t)0xFCD2);

      if (advertisedDevice.haveServiceData() && advertisedDevice.getServiceDataUUID().equals(bthomeUUID)) {
        int rssi = advertisedDevice.getRSSI();
        String currentMac = advertisedDevice.getAddress().toString().c_str();
        currentMac.toLowerCase();

        float t = 0.0, h = 0.0, v = 0.0;
        int b = 0;
        String sData = advertisedDevice.getServiceData();

        if (parseBTHome((const uint8_t*)sData.c_str(), sData.length(), t, h, b, v)) {
          String devName = advertisedDevice.getName().c_str();
          if (devName.length() == 0) {
            String cleanMac = currentMac;
            cleanMac.replace(":", "");
            if (cleanMac.length() >= 6) {
              devName = "ATC_" + cleanMac.substring(cleanMac.length() - 6);
              devName.toUpperCase();
            } else {
              devName = "ATC_DEVICE";
            }
          }

          bool foundInList = false;
          for (auto& item : discoveredBLEs) {
            if (item.mac.equalsIgnoreCase(currentMac)) {
              item.rssi = rssi;
              item.temp = t;
              item.hum = h;
              item.battery = b;
              foundInList = true;
              break;
            }
          }
          if (!foundInList && discoveredBLEs.size() < 10) {
            discoveredBLEs.push_back({currentMac, devName, rssi, t, h, b});
          }

          if (cfgMgr.config.bleTargetMac.length() > 0 && sysMode == MODE_NORMAL_RUN) {
            if (currentMac.equalsIgnoreCase(cfgMgr.config.bleTargetMac)) {
              float rawT = t;
              measuredTemp = cfgMgr.applyCalibration(rawT);
              measuredHum = h;
              measuredBattery = b;
              measuredVoltage = v;
              measuredRssi = rssi;
              measuredDeviceName = cfgMgr.config.bleTargetName;
              measuredMacAddress = currentMac;

              lastDispTemp = measuredTemp;
              lastDispHum = h;
              lastDispBattery = b;
              lastDispVoltage = v;
              lastDispRssi = rssi;
              everReceivedAnyData = true;
              hasFreshData = true;
              bleConnectedStatus = true;
              lastBlePacketReceivedTime = millis();
            }
          }
          else {
            if (rssi > discoveryBestRssi) {
              discoveryBestRssi = rssi;
              candidateMac = currentMac;
              candidateName = devName;
              candidateTemp = t;
              candidateHum = h;
              candidateBattery = b;
              candidateVoltage = v;
              foundCandidate = true;
            }
          }
        }
      }
    }
};

static MyAdvertisedDeviceCallbacks s_bleCallbacks;

void startBLEScanForMode() {
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);

  if (pBLEScan == nullptr) {
    Serial.printf("[BLE] Initializing BLE stack (Free Heap before BLE: %u bytes)...\n", ESP.getFreeHeap());
    BLEDevice::init("");

    // Maksimum BLE RF Gücü (+20 dBm = 100 mW)
    esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_DEFAULT, ESP_PWR_LVL_P20);
    esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_SCAN, ESP_PWR_LVL_P20);
    esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_ADV, ESP_PWR_LVL_P20);

    pBLEScan = BLEDevice::getScan();
    pBLEScan->setAdvertisedDeviceCallbacks(&s_bleCallbacks, true);
    pBLEScan->setActiveScan(true);
    pBLEScan->setInterval(100);
    pBLEScan->setWindow(100); // 100% continuous duty cycle
    Serial.printf("[BLE] BLE scan active (TX Power: +20 dBm Max, Free Heap: %u bytes, MaxAlloc: %u bytes)\n",
                  ESP.getFreeHeap(), ESP.getMaxAllocHeap());
  }
}

void stopBLE() {
  if (pBLEScan) {
    pBLEScan->stop();
    pBLEScan->clearResults();
  }
}

void stopBLEAndFreeMem() {
  stopBLE();
  Serial.printf("[BLE] Releasing BLE stack to free RAM for Wi-Fi/HTTPS (Free Heap before deinit: %u bytes)...\n", ESP.getFreeHeap());
  BLEDevice::deinit(false); // false = do NOT release controller ROM, permits reinit via BLEDevice::init("")
  pBLEScan = nullptr;
  Serial.printf("[BLE] ✅ BLE stack released (Free Heap now: %u bytes, MaxAlloc: %u bytes)\n",
                ESP.getFreeHeap(), ESP.getMaxAllocHeap());
}

void WiFiEvent(WiFiEvent_t event, WiFiEventInfo_t info) {
  switch (event) {
    case ARDUINO_EVENT_WPS_ER_SUCCESS:
      wpsSuccess = true;
      break;
    case ARDUINO_EVENT_WPS_ER_FAILED:
      break;
    case ARDUINO_EVENT_WPS_ER_TIMEOUT:
      break;
    default:
      break;
  }
}

void startWPSProcess() {
  stopBLE();
  BLEDevice::deinit(true);
  pBLEScan = nullptr;
  WiFi.mode(WIFI_STA);
  WiFi.disconnect(true);

  WiFi.onEvent(WiFiEvent);
  wps_config.wps_type = WPS_TYPE_PBC;

  esp_wifi_wps_enable(&wps_config);
  esp_wifi_wps_start(0);

  Serial.println("\n[WPS] WPS PBC Started. Please press WPS button on your router...");
}

const char* getWiFiStatusText(wl_status_t status) {
  switch (status) {
    case WL_IDLE_STATUS: return "IDLE";
    case WL_NO_SSID_AVAIL: return "NO_SSID_AVAIL (SSID not found)";
    case WL_SCAN_COMPLETED: return "SCAN_COMPLETED";
    case WL_CONNECTED: return "CONNECTED";
    case WL_CONNECT_FAILED: return "CONNECT_FAILED (Auth/Password error?)";
    case WL_CONNECTION_LOST: return "CONNECTION_LOST";
    case WL_DISCONNECTED: return "DISCONNECTED";
    default: return "UNKNOWN";
  }
}

void sendTelegramMessage(String msg) {
  if (cfgMgr.config.telegramBotToken.length() == 0) {
    Serial.println("[Telegram] ℹ️ Telegram not configured (Bot Token empty in NVS).");
    return;
  }

  // Count active recipients
  int activeCount = 0;
  for (int i = 0; i < MAX_TG_RECIPIENTS; i++) {
    if (cfgMgr.config.tgRecipients[i].chatId.length() > 0) activeCount++;
  }

  // Fallback to legacy telegramChatId if array is empty
  if (activeCount == 0 && cfgMgr.config.telegramChatId.length() > 0) {
    cfgMgr.config.tgRecipients[0].chatId = cfgMgr.config.telegramChatId;
    cfgMgr.config.tgRecipients[0].note = "Primary";
    activeCount = 1;
  }

  if (activeCount == 0) {
    Serial.println("[Telegram] ℹ️ No Telegram Chat IDs configured.");
    return;
  }

  Serial.printf("[Telegram] 📤 Dispatching alert broadcast to %d recipient(s)...\n", activeCount);
  String encodedMsg = urlEncode(msg);
  int sentSuccess = 0;
  int currentIdx = 0;

  for (int i = 0; i < MAX_TG_RECIPIENTS; i++) {
    String chatId = cfgMgr.config.tgRecipients[i].chatId;
    chatId.trim();
    if (chatId.length() == 0) continue;
    currentIdx++;

    String note = cfgMgr.config.tgRecipients[i].note;
    note.trim();
    if (note.length() == 0) note = "Recipient #" + String(i + 1);

    Serial.printf("[Telegram] ➡️ [%d/%d] Sending to: '%s' (ID: %s)...\n",
                  currentIdx, activeCount, note.c_str(), chatId.c_str());

    NetworkClientSecure client;
    client.setInsecure();
    client.setHandshakeTimeout(10);
    HTTPClient https;

    String url = "https://api.telegram.org/bot" + cfgMgr.config.telegramBotToken +
                 "/sendMessage?chat_id=" + chatId +
                 "&text=" + encodedMsg;

    if (https.begin(client, url)) {
      https.setTimeout(8000);
      unsigned long tStart = millis();
      int code = https.GET();
      unsigned long dur = millis() - tStart;

      if (code == 200) {
        sentSuccess++;
        Serial.printf("[Telegram] ✅ Sent to '%s' (took %lu ms)\n", note.c_str(), dur);
      } else {
        Serial.printf("[Telegram] ⚠️ Send to '%s' failed! Code: %d (%s, took %lu ms)\n",
                      note.c_str(), code, https.errorToString(code).c_str(), dur);
        if (code > 0) {
          String resp = https.getString();
          Serial.printf("[Telegram] 🔍 Response: %s\n", resp.c_str());
        }
      }
      https.end();
    } else {
      Serial.printf("[Telegram] ❌ https.begin() failed for '%s'\n", note.c_str());
    }

    if (currentIdx < activeCount) delay(120);
  }

  Serial.printf("[Telegram] 🏁 Broadcast complete: %d/%d recipients delivered.\n", sentSuccess, activeCount);
}

bool sendTelegramReply(const String& chatId, const String& msg) {
  if (cfgMgr.config.telegramBotToken.length() == 0 || chatId.length() == 0) return false;

  Serial.printf("[TG-Bot] 📤 Replying to %s: %s\n", chatId.c_str(), msg.substring(0, 30).c_str());
  String encodedMsg = urlEncode(msg);

  NetworkClientSecure client;
  client.setInsecure();
  client.setHandshakeTimeout(10);
  HTTPClient https;

  String url = "https://api.telegram.org/bot" + cfgMgr.config.telegramBotToken +
               "/sendMessage?chat_id=" + chatId +
               "&text=" + encodedMsg;

  bool ok = false;
  if (https.begin(client, url)) {
    https.setTimeout(8000);
    int code = https.GET();
    if (code == 200) {
      ok = true;
      Serial.println("[TG-Bot] ✅ Reply delivered.");
    } else {
      Serial.printf("[TG-Bot] ⚠️ Reply failed! Code: %d (%s)\n", code, https.errorToString(code).c_str());
    }
    https.end();
  }
  return ok;
}

void handleTelegramBotCommand(const String& chatId, String cmd) {
  // 1. Authorization check
  bool isAuth = false;
  int authCount = 0;
  for (int i = 0; i < MAX_TG_RECIPIENTS; i++) {
    String regId = cfgMgr.config.tgRecipients[i].chatId;
    regId.trim();
    if (regId.length() > 0) {
      authCount++;
      if (regId == chatId) { isAuth = true; break; }
    }
  }
  if (!isAuth && cfgMgr.config.telegramChatId.length() > 0) {
    authCount++;
    if (cfgMgr.config.telegramChatId == chatId) isAuth = true;
  }

  // If no chat IDs are configured yet, allow the sender to interact
  if (authCount == 0) isAuth = true;

  if (!isAuth) {
    Serial.printf("[TG-Bot] ⛔ Unauthorized command attempt from Chat ID: %s\n", chatId.c_str());
    String unauthMsg = "⛔ YETKİSİZ ERİŞİM!\n\n"
                       "Bu cihazın yönetim yetkisine sahip değilsiniz.\n"
                       "Chat ID'niz: " + chatId + "\n\n"
                       "Cihaz yöneticisi bu Chat ID'yi cihazın web arayüzünden eklemelidir.";
    sendTelegramReply(chatId, unauthMsg);
    return;
  }

  // 2. Command normalization
  cmd.trim();
  if (cmd.startsWith("/")) cmd = cmd.substring(1);
  int atPos = cmd.indexOf('@');
  if (atPos != -1) cmd = cmd.substring(0, atPos);
  cmd.trim();
  String lowerCmd = cmd;
  lowerCmd.toLowerCase();

  Serial.printf("[TG-Bot] ⚙️ Processing command: '%s' (raw: '%s')\n", lowerCmd.c_str(), cmd.c_str());

  // --- COMMAND 1: DURUM / STATUS / BILGI ---
  if (lowerCmd == "durum" || lowerCmd == "status" || lowerCmd == "bilgi" || lowerCmd == "info") {
    String reply = "📊 SİSTEM GENEL DURUMU\n";
    reply += "━━━━━━━━━━━━━━━━━━━━\n";
    if (hasFreshData) {
      reply += "🌡️ Sıcaklık: " + String(measuredTemp, 2) + " °C\n";
      reply += "💧 Bağıl Nem: %" + String(measuredHum, 1) + "\n";
      reply += "🔋 Sensör Pili: %" + String(measuredBattery) + " (" + String(measuredVoltage, 2) + " V)\n";
    } else {
      reply += "🌡️ Sıcaklık: Sensör verisi bekleniyor...\n";
    }

    float tMin = isPowerOutage ? cfgMgr.config.powerLossTempMin : cfgMgr.config.normalTempMin;
    float tMax = isPowerOutage ? cfgMgr.config.powerLossTempMax : cfgMgr.config.normalTempMax;
    reply += "🎯 Güvenli Bölge: " + String(tMin, 1) + " - " + String(tMax, 1) + " °C\n";

    reply += "⚡ Şebeke Gücü: " + String(isPowerOutage ? "⚠️ KESİNTİ (Pilde)" : "✅ ŞEBEKE AKTİF") + "\n";
    reply += "🔵 BLE Cihaz: " + (cfgMgr.config.bleTargetName.length() > 0 ? cfgMgr.config.bleTargetName : "Otomatik Bul") +
             " (" + (bleConnectedStatus ? "Bağlı" : "Aranıyor") + ")\n";

    IPAddress localIp = WiFi.localIP();
    reply += "📶 Wi-Fi: " + WiFi.SSID() + " (" + String(WiFi.RSSI()) + " dBm)\n";
    reply += "🌐 Cihaz IP: " + localIp.toString() + "\n";

    time_t nowSec = time(nullptr);
    if (nowSec > 1000000000) {
      struct tm* t = localtime(&nowSec);
      char buf[32];
      snprintf(buf, sizeof(buf), "%02d.%02d.%04d %02d:%02d",
               t->tm_mday, t->tm_mon + 1, t->tm_year + 1900,
               t->tm_hour, t->tm_min);
      reply += "🕒 Saat: " + String(buf) + "\n";
    }
    reply += "━━━━━━━━━━━━━━━━━━━━\n";
    reply += "💡 Rapor özeti için 'rapor', komut listesi için 'yardim' yazabilirsiniz.";
    sendTelegramReply(chatId, reply);
    return;
  }

  // --- COMMAND 2: SICAKLIK / ISI / TEMP ---
  if (lowerCmd == "sicaklik" || lowerCmd == "sıcaklık" || lowerCmd == "isi" ||
      lowerCmd == "ısı" || lowerCmd == "temp" || lowerCmd == "derece") {
    String reply = "🌡️ ANLIK SICAKLIK BİLGİSİ\n";
    reply += "━━━━━━━━━━━━━━━━━━━━\n";
    if (hasFreshData) {
      reply += "Anlık Sıcaklık: " + String(measuredTemp, 2) + " °C\n";
      reply += "Bağıl Nem: %" + String(measuredHum, 1) + "\n";
      float tMin = isPowerOutage ? cfgMgr.config.powerLossTempMin : cfgMgr.config.normalTempMin;
      float tMax = isPowerOutage ? cfgMgr.config.powerLossTempMax : cfgMgr.config.normalTempMax;
      reply += "Hedef Aralık: " + String(tMin, 1) + " °C ile " + String(tMax, 1) + " °C arası\n";
      if (measuredTemp < tMin) {
        reply += "Durum: ❄️ DÜŞÜK SICAKLIK ALARMI!\n";
      } else if (measuredTemp > tMax) {
        reply += "Durum: 🔥 YÜKSEK SICAKLIK ALARMI!\n";
      } else {
        reply += "Durum: ✅ NORMAL (Güvenli Bölgede)\n";
      }
    } else {
      reply += "⚠️ Termometreden henüz yeni veri okunamadı.";
    }
    sendTelegramReply(chatId, reply);
    return;
  }

  // --- COMMAND 3: RAPOR / REPORT / ISTATISTIK ---
  if (lowerCmd == "rapor" || lowerCmd == "report" || lowerCmd == "istatistik" ||
      lowerCmd == "ozet" || lowerCmd == "özet") {
    String reply = "📈 SOĞUK ZİNCİR KAYIT ÖZETİ\n";
    reply += "━━━━━━━━━━━━━━━━━━━━\n";
    if (historyCount == 0) {
      reply += "Hafızada henüz kayıtlı geçmiş veri bulunmuyor.";
    } else {
      float minT = 999.0f, maxT = -999.0f, sumT = 0.0f;
      int violations = 0;
      int validCount = 0;

      float tMin = cfgMgr.config.normalTempMin;
      float tMax = cfgMgr.config.normalTempMax;

      int startIdx = (historyCount < HISTORY_SIZE) ? 0 : historyHead;
      for (int i = 0; i < historyCount; i++) {
        int idx = (startIdx + i) % HISTORY_SIZE;
        float t = tempHistory[idx].temp / 10.0f;
        if (t < minT) minT = t;
        if (t > maxT) maxT = t;
        sumT += t;
        validCount++;
        if (t < tMin || t > tMax) violations++;
      }

      float avgT = (validCount > 0) ? (sumT / validCount) : 0.0f;
      reply += "Kayıt Sayısı: " + String(historyCount) + " ölçüm\n";
      reply += "En Düşük: " + String(minT, 1) + " °C\n";
      reply += "En Yüksek: " + String(maxT, 1) + " °C\n";
      reply += "Ortalama: " + String(avgT, 1) + " °C\n";
      reply += "Limit İhlali: " + String(violations) + " adet\n";
      reply += "━━━━━━━━━━━━━━━━━━━━\n";
      if (violations == 0) {
        reply += "✅ Soğuk zincir standartlarına tam uyumlu.";
      } else {
        reply += "⚠️ " + String(violations) + " adet sıcaklık sınırı aşımı kaydedildi!";
      }
    }
    sendTelegramReply(chatId, reply);
    return;
  }

  // --- COMMAND 4: LINK / IP / WEB / PORTAL ---
  if (lowerCmd == "link" || lowerCmd == "ip" || lowerCmd == "web" || lowerCmd == "portal") {
    IPAddress localIp = WiFi.localIP();
    String reply = "🌐 CİHAZ ERİŞİM BİLGİLERİ\n";
    reply += "━━━━━━━━━━━━━━━━━━━━\n";
    reply += "Wi-Fi Ağı: " + WiFi.SSID() + "\n";
    reply += "Cihaz IP: " + localIp.toString() + "\n\n";
    reply += "📄 PDF Raporu (Aynı Wi-Fi ağından):\n";
    reply += "http://" + localIp.toString() + "/report?range=24h\n\n";
    reply += "📶 AP Ayar Portalı:\n";
    reply += "SSID: Thermo_Obs\n";
    reply += "Şifre: " + cfgMgr.config.apPassword + "\n";
    reply += "Adres: http://192.168.4.1";
    sendTelegramReply(chatId, reply);
    return;
  }

  // --- COMMAND 5: YARDIM / HELP / START / MENU ---
  if (lowerCmd == "yardim" || lowerCmd == "yardım" || lowerCmd == "help" ||
      lowerCmd == "komutlar" || lowerCmd == "start" || lowerCmd == "menu") {
    String reply = "🤖 THERMO_OBS BOT KOMUTLARI\n";
    reply += "━━━━━━━━━━━━━━━━━━━━\n";
    reply += "Aşağıdaki kelimelerden birini yazabilirsiniz:\n\n";
    reply += "🔹 durum : Anlık sıcaklık, pil, şebeke ve sistem özeti\n";
    reply += "🔹 sicaklik : Sadece anlık ısı ve limit analizi\n";
    reply += "🔹 rapor : Kayıtlı verilerin Min/Max/Ortalama analizi\n";
    reply += "🔹 link : Cihaz IP ve PDF rapor erişim bağlantısı\n";
    reply += "🔹 yardim : Bu komut yardım menüsü\n";
    reply += "━━━━━━━━━━━━━━━━━━━━\n";
    reply += "💡 Komutların başına '/' koyabilir veya direkt kelime olarak yazabilirsiniz.";
    sendTelegramReply(chatId, reply);
    return;
  }

  // --- UNKNOWN COMMAND FALLBACK ---
  String reply = "❓ '" + cmd + "' komutu anlaşılamadı.\n\n"
                 "Kullanabileceğiniz komutlar:\n"
                 "👉 durum\n👉 sicaklik\n👉 rapor\n👉 link\n👉 yardim";
  sendTelegramReply(chatId, reply);
}

void processTelegramIncomingMessages() {
  if (cfgMgr.config.telegramBotToken.length() == 0) return;

  Serial.println("\n--- [ 3. Telegram Bot Inbound Polling ] ---");

  NetworkClientSecure client;
  client.setInsecure();
  client.setHandshakeTimeout(10);
  HTTPClient https;

  String url = "https://api.telegram.org/bot" + cfgMgr.config.telegramBotToken + "/getUpdates";
  if (lastTelegramUpdateId > 0) {
    url += "?offset=" + String(lastTelegramUpdateId) + "&limit=5&timeout=0";
  } else {
    url += "?offset=-3&limit=3&timeout=0";
  }

  if (!https.begin(client, url)) {
    Serial.println("[TG-Bot] ❌ https.begin() failed for getUpdates.");
    return;
  }

  https.setTimeout(6000);
  int httpCode = https.GET();
  if (httpCode != 200) {
    Serial.printf("[TG-Bot] ⚠️ getUpdates failed with code: %d\n", httpCode);
    https.end();
    return;
  }

  String payload = https.getString();
  https.end();

  if (payload.indexOf("\"ok\":true") == -1 || payload.indexOf("\"result\":[]") != -1) {
    Serial.println("[TG-Bot] 📭 No new messages.");
    return;
  }

  Serial.printf("[TG-Bot] 📬 Updates received (%d bytes). Parsing...\n", payload.length());

  int searchPos = 0;
  int processedCount = 0;

  while (processedCount < 5) {
    int upPos = payload.indexOf("\"update_id\":", searchPos);
    if (upPos == -1) break;

    // 1. Extract update_id
    int upValStart = upPos + 12;
    int upValEnd = payload.indexOf(',', upValStart);
    if (upValEnd == -1) upValEnd = payload.indexOf('}', upValStart);
    if (upValEnd == -1) break;

    long updateId = payload.substring(upValStart, upValEnd).toInt();
    if (updateId >= lastTelegramUpdateId) {
      lastTelegramUpdateId = updateId + 1;
    }

    int nextUpPos = payload.indexOf("\"update_id\":", upValEnd);
    int itemEnd = (nextUpPos != -1) ? nextUpPos : payload.length();

    // 2. Extract Chat ID (look for "chat":{..."id":...)
    String chatId = "";
    int chatPos = payload.indexOf("\"chat\":", upValEnd);
    if (chatPos != -1 && chatPos < itemEnd) {
      int idPos = payload.indexOf("\"id\":", chatPos);
      if (idPos != -1 && idPos < itemEnd) {
        int idStart = idPos + 5;
        int idEnd = payload.indexOf(',', idStart);
        int idEndBrace = payload.indexOf('}', idStart);
        if (idEnd == -1 || (idEndBrace != -1 && idEndBrace < idEnd)) idEnd = idEndBrace;
        if (idEnd != -1 && idEnd < itemEnd) {
          chatId = payload.substring(idStart, idEnd);
          chatId.trim();
        }
      }
    }

    // 3. Extract Text
    String msgText = "";
    int textPos = payload.indexOf("\"text\":\"", upValEnd);
    if (textPos != -1 && textPos < itemEnd) {
      int tStart = textPos + 8;
      int tEnd = tStart;
      while (tEnd < itemEnd) {
        if (payload[tEnd] == '"' && payload[tEnd - 1] != '\\') break;
        tEnd++;
      }
      if (tEnd < itemEnd) {
        msgText = payload.substring(tStart, tEnd);
        msgText.replace("\\\"", "\"");
        msgText.replace("\\/", "/");
      }
    }

    searchPos = upValEnd;

    if (chatId.length() == 0 || msgText.length() == 0) continue;

    processedCount++;
    Serial.printf("[TG-Bot] 📩 Incoming message #%d from Chat %s: '%s'\n",
                  processedCount, chatId.c_str(), msgText.c_str());

    handleTelegramBotCommand(chatId, msgText);
    delay(100);
  }
}

void sendWebhookMessage(String jsonPayload) {
  if (cfgMgr.config.webhookUrl.length() == 0) return;

  Serial.printf("[Webhook] 📤 Sending POST to: %s\n", cfgMgr.config.webhookUrl.c_str());
  NetworkClientSecure client;
  client.setInsecure();
  HTTPClient https;

  if (https.begin(client, cfgMgr.config.webhookUrl)) {
    https.addHeader("Content-Type", "application/json");
    https.setTimeout(10000);
    int code = https.POST(jsonPayload);
    if (code > 0) {
      Serial.printf("[Webhook] ✅ Response: %d\n", code);
    } else {
      Serial.printf("[Webhook] ❌ POST failed! Error: %s (code %d)\n", https.errorToString(code).c_str(), code);
    }
    https.end();
  } else {
    Serial.println("[Webhook] ❌ https.begin() failed!");
  }
}

bool checkMenuAbort() {
  handleButtonState();
  if (btnIsPressed && sysMode == MODE_NORMAL_RUN) {
    unsigned long heldTime = millis() - btnPressStart;
    if (heldTime >= 1500) {
      drawNormalScreens();
    }
  }
  return (sysMode == MODE_MENU || isBrowsingScreens);
}

bool connectToAvailableWiFi() {
  Serial.println("\n[WIFI] ================= Wi-Fi Connection Phase ================");
  Serial.printf("[WIFI] Free Heap: %u bytes (Min Ever: %u bytes)\n", ESP.getFreeHeap(), ESP.getMinFreeHeap());

  bool hasPrimary = (cfgMgr.config.wifiSsid.length() > 0 && cfgMgr.config.wifiSsid != "YOUR_WIFI_SSID");
  bool hasBackup = (cfgMgr.config.backupWifiSsid.length() > 0);

  if (!hasPrimary && !hasBackup) {
    Serial.println("[WIFI] ❌ No Wi-Fi credentials configured! (Primary SSID is empty or default).");
    Serial.println("[WIFI] Please enter Web Portal (5s button press) and save your Wi-Fi SSID & Password.");
    Serial.println("[WIFI] ==========================================================\n");
    wifiConnectedStatus = false;
    return false;
  }

  if (hasPrimary) {
    Serial.printf("[WIFI] [1/2] Connecting to Primary SSID: '%s'...\n", cfgMgr.config.wifiSsid.c_str());
    WiFi.disconnect(true);
    delay(50);
    WiFi.begin(cfgMgr.config.wifiSsid.c_str(), cfgMgr.config.wifiPass.c_str());

    unsigned long start = millis();
    int dots = 0;
    while (WiFi.status() != WL_CONNECTED && (millis() - start < 15000)) {
      delay(500);
      Serial.print(".");
      dots++;
      if (dots % 20 == 0) Serial.println();
      if (checkMenuAbort()) {
        Serial.println("\n[WIFI] ⏹️ Connection cancelled by user button press.");
        WiFi.disconnect(true);
        wifiConnectedStatus = false;
        return false;
      }
    }
    Serial.println();

    if (WiFi.status() == WL_CONNECTED) {
      wifiConnectedStatus = true;
      lastWiFiSuccess = true;
      lastAssignedIp = WiFi.localIP().toString();
      // Inject Google 8.8.8.8 and Cloudflare 1.1.1.1 DNS servers to avoid router DNS lockups
      IPAddress dns1(8, 8, 8, 8);
      IPAddress dns2(1, 1, 1, 1);
      WiFi.config(WiFi.localIP(), WiFi.gatewayIP(), WiFi.subnetMask(), dns1, dns2);

      Serial.printf("[WIFI] ✅ Connected to Primary SSID: '%s'\n", cfgMgr.config.wifiSsid.c_str());
      Serial.printf("[WIFI] 📍 IP: %s | Gateway: %s | DNS: 8.8.8.8 | RSSI: %d dBm\n",
                    WiFi.localIP().toString().c_str(), WiFi.gatewayIP().toString().c_str(), WiFi.RSSI());
      Serial.println("[WIFI] ==========================================================\n");
      if (!isTimeSynced || (millis() - lastNtpSyncTime >= 86400000UL)) syncNtpTime();
      return true;
    } else {
      Serial.printf("[WIFI] ⚠️ Primary connection failed after 15s! Reason: %s (status %d)\n",
                    getWiFiStatusText(WiFi.status()), (int)WiFi.status());
    }
  }

  if (hasBackup) {
    Serial.printf("[WIFI] [2/2] Connecting to Backup SSID: '%s'...\n", cfgMgr.config.backupWifiSsid.c_str());
    WiFi.disconnect(true);
    delay(100);
    WiFi.begin(cfgMgr.config.backupWifiSsid.c_str(), cfgMgr.config.backupWifiPass.c_str());

    unsigned long start = millis();
    int dots = 0;
    while (WiFi.status() != WL_CONNECTED && (millis() - start < 15000)) {
      delay(500);
      Serial.print(".");
      dots++;
      if (dots % 20 == 0) Serial.println();
      if (checkMenuAbort()) {
        Serial.println("\n[WIFI] ⏹️ Connection cancelled by user button press.");
        WiFi.disconnect(true);
        wifiConnectedStatus = false;
        return false;
      }
    }
    Serial.println();

    if (WiFi.status() == WL_CONNECTED) {
      wifiConnectedStatus = true;
      lastWiFiSuccess = true;
      lastAssignedIp = WiFi.localIP().toString();
      IPAddress dns1(8, 8, 8, 8);
      IPAddress dns2(1, 1, 1, 1);
      WiFi.config(WiFi.localIP(), WiFi.gatewayIP(), WiFi.subnetMask(), dns1, dns2);

      Serial.printf("[WIFI] ✅ Connected to Backup SSID: '%s'\n", cfgMgr.config.backupWifiSsid.c_str());
      Serial.printf("[WIFI] 📍 IP: %s | Gateway: %s | DNS: 8.8.8.8 | RSSI: %d dBm\n",
                    WiFi.localIP().toString().c_str(), WiFi.gatewayIP().toString().c_str(), WiFi.RSSI());
      Serial.println("[WIFI] ==========================================================\n");
      if (!isTimeSynced || (millis() - lastNtpSyncTime >= 86400000UL)) syncNtpTime();
      return true;
    } else {
      Serial.printf("[WIFI] ⚠️ Backup connection failed after 15s! Reason: %s (status %d)\n",
                    getWiFiStatusText(WiFi.status()), (int)WiFi.status());
    }
  }

  Serial.println("[WIFI] ❌ All Wi-Fi connection attempts failed.");
  Serial.println("[WIFI] ==========================================================\n");
  wifiConnectedStatus = false;
  lastWiFiSuccess = false;
  return false;
}

void executeSendCycle() {
  Serial.println("\n################### [ SEND CYCLE START ] ###################");
  Serial.printf("[CYCLE] Timestamp: %lu ms\n", millis());
  Serial.printf("[CYCLE] Telemetry Data: Temp=%.2f C, Hum=%.1f %%, Bat=%d %%, Volt=%.2f V, RSSI=%d dBm\n",
                measuredTemp, measuredHum, measuredBattery, measuredVoltage, measuredRssi);
  Serial.printf("[CYCLE] Fresh BLE Data: %s | Power: %s\n",
                hasFreshData ? "YES" : "NO", isPowerOutage ? "OUTAGE (Battery)" : "ONLINE (Mains)");
  Serial.printf("[CYCLE] Free Heap before stopping BLE: %u bytes, MaxAlloc: %u bytes\n", ESP.getFreeHeap(), ESP.getMaxAllocHeap());

  stopBLEAndFreeMem();
  delay(50);
  Serial.printf("[CYCLE] Free Heap after stopping BLE: %u bytes, MaxAlloc: %u bytes\n", ESP.getFreeHeap(), ESP.getMaxAllocHeap());

  WiFi.mode(WIFI_STA);

  if (connectToAvailableWiFi()) {
    if (checkMenuAbort()) {
      Serial.println("[CYCLE] ⏹️ Aborted during Wi-Fi connection. Disconnecting Wi-Fi.");
      WiFi.disconnect(true);
      WiFi.mode(WIFI_OFF);
      return;
    }

    // --- 1. GOOGLE SHEETS TELEMETRY ---
    Serial.println("\n--- [ 1. Google Sheets Transmission ] ---");
    if (cfgMgr.config.googleScriptUrl.length() > 0 && cfgMgr.config.googleScriptUrl.indexOf("YOUR_SCRIPT_ID") == -1) {
      Serial.println("[Sheets] 🌐 Preparing Google Apps Script request...");

      // Explicit DNS Resolution Verification
      IPAddress scriptIp;
      bool dnsSuccess = WiFi.hostByName("script.google.com", scriptIp);
      Serial.printf("[DNS] Resolving 'script.google.com' -> %s (Success: %s)\n",
                    dnsSuccess ? scriptIp.toString().c_str() : "0.0.0.0",
                    dnsSuccess ? "YES" : "FAILED");

      NetworkClientSecure client;
      client.setInsecure();
      client.setHandshakeTimeout(15);
      HTTPClient https;

      String dName = (cfgMgr.config.bleTargetName.length() > 0) ? cfgMgr.config.bleTargetName : measuredDeviceName;
      String url = cfgMgr.config.googleScriptUrl + "?device=" + urlEncode(dName);

      if (hasFreshData) {
        url += "&temp=" + String(measuredTemp, 2) +
               "&hum=" + String(measuredHum, 1) +
               "&bat=" + String(measuredBattery) +
               "&volt=" + String(measuredVoltage, 2) +
               "&rssi=" + String(measuredRssi) +
               "&pwr=" + String(isPowerOutage ? "OUTAGE" : "ONLINE") +
               "&note=Normal";
      } else {
        url += "&temp=-&hum=-&bat=-&volt=-&rssi=-&pwr=" + String(isPowerOutage ? "OUTAGE" : "ONLINE") + "&note=No+BLE+Connection";
      }

      Serial.printf("[Sheets] 🔗 URL: %s\n", url.c_str());
      Serial.printf("[Sheets] 📡 Connecting via HTTPS... (Free Heap: %u bytes, Max Alloc: %u bytes, Min Ever: %u bytes)\n",
                    ESP.getFreeHeap(), ESP.getMaxAllocHeap(), ESP.getMinFreeHeap());

      if (https.begin(client, url)) {
        https.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
        https.setTimeout(15000); // 15 seconds for serverless Google Apps Script execution and redirects
        unsigned long httpStart = millis();
        int code = https.GET();
        unsigned long httpDur = millis() - httpStart;

        if (code > 0) {
          Serial.printf("[Sheets] ✅ HTTP Success! Status Code: %d (took %lu ms)\n", code, httpDur);
          String respBody = https.getString();
          if (respBody.length() > 0) {
            String preview = respBody.substring(0, 200);
            preview.replace("\r", "");
            preview.replace("\n", " ");
            Serial.printf("[Sheets] 📄 Response Payload: %s%s\n",
                          preview.c_str(), respBody.length() > 200 ? "..." : "");
          }
        } else {
          char errBuf[128] = {0};
          client.lastError(errBuf, sizeof(errBuf));
          Serial.printf("[Sheets] ❌ HTTP GET Failed! Error: %s (Code: %d, took %lu ms)\n",
                        https.errorToString(code).c_str(), code, httpDur);
          if (strlen(errBuf) > 0) {
            Serial.printf("[Sheets] 🔍 TLS Client Diagnostic: %s\n", errBuf);
          }
          Serial.println("[Sheets] 💡 Note: Code -1 = DNS / SSL handshake failure, -11 = Read Timeout.");
        }
        https.end();
      } else {
        Serial.println("[Sheets] ❌ https.begin() failed! Invalid URL or failed TLS client init.");
      }
    } else {
      Serial.println("[Sheets] ℹ️ Google Sheets URL not configured or has default placeholder. Skipping.");
    }

    if (checkMenuAbort()) {
      Serial.println("[CYCLE] ⏹️ Aborted before alert evaluation.");
      WiFi.disconnect(true);
      WiFi.mode(WIFI_OFF);
      return;
    }

    // --- 2. TELEGRAM ALERTS ---
    Serial.println("\n--- [ 2. Telegram Alert Evaluation ] ---");
    unsigned long now = millis();

    // 1. Mains Power Outage & Restored (Independent of temperature)
    if (!hasInitializedPowerState) {
      wasPowerOutage = isPowerOutage;
      hasInitializedPowerState = true;
      Serial.printf("[Alert] Initialized power state: %s\n", isPowerOutage ? "OUTAGE" : "ONLINE");
    } else {
      if (isPowerOutage && !wasPowerOutage) {
        wasPowerOutage = true;
        lastPowerAlertTime = now;
        Serial.println("[Alert] ⚡ Mains power outage detected! Sending alert...");
        if (cfgMgr.config.notifyTelegramOnPowerLoss) {
          sendTelegramMessage("⚡ MAINS POWER OUTAGE DETECTED!\nElectricity cut off.\nDevice running on battery.\nCurrent Temp: " + String(measuredTemp, 2) + " °C");
        }
      } else if (!isPowerOutage && wasPowerOutage) {
        wasPowerOutage = false;
        Serial.println("[Alert] 🔌 Mains power restored! Sending recovery notification...");
        if (cfgMgr.config.notifyTelegramOnPowerLoss) {
          sendTelegramMessage("🔌 MAINS POWER RESTORED!\nGrid electricity is back online.\nCurrent Temp: " + String(measuredTemp, 2) + " °C");
        }
      } else if (isPowerOutage && (now - lastPowerAlertTime >= (unsigned long)cfgMgr.config.powerLossAlertIntervalMin * 60000UL)) {
        lastPowerAlertTime = now;
        Serial.println("[Alert] ⚠️ Power outage ongoing reminder sending...");
        if (cfgMgr.config.notifyTelegramOnPowerLoss) {
          sendTelegramMessage("⚠️ POWER OUTAGE ONGOING!\nStill running on battery.\nCurrent Temp: " + String(measuredTemp, 2) + " °C");
        }
      }
    }

    // 2. BLE Thermometer Connection & Disconnection Alerts
    if (!hasInitializedBleState) {
      wasBleConnected = bleConnectedStatus;
      hasInitializedBleState = true;
      Serial.printf("[Alert] Initialized BLE state: %s\n", bleConnectedStatus ? "CONNECTED" : "DISCONNECTED");
    } else {
      if (!bleConnectedStatus && wasBleConnected) {
        wasBleConnected = false;
        Serial.println("[Alert] ⚠️ BLE connection lost! Sending alert...");
        sendTelegramMessage("⚠️ BLE SENSOR DISCONNECTED!\nNo packet received from thermometer.\nDevice: " + cfgMgr.config.bleTargetName + " (" + cfgMgr.config.bleTargetMac + ")");
      } else if (bleConnectedStatus && !wasBleConnected) {
        wasBleConnected = true;
        Serial.println("[Alert] ✅ BLE sensor reconnected! Sending notification...");
        sendTelegramMessage("✅ BLE SENSOR RECONNECTED!\nThermometer telemetry restored.\nDevice: " + cfgMgr.config.bleTargetName + "\nCurrent Temp: " + String(measuredTemp, 2) + " °C");
      }
    }

    // 3. NTP Timestamp Sync Error Alert
    if (!isTimeSynced && ntpFailedWarning) {
      if (now - lastNtpErrorAlertTime >= 3600000UL) {
        lastNtpErrorAlertTime = now;
        Serial.println("[Alert] ⚠️ NTP sync failed. Sending Telegram notification...");
        sendTelegramMessage("⚠️ NTP TIME SYNC ERROR!\nFailed to synchronize clock from NTP servers.\nDevice timestamp is unavailable.");
      }
    }

    // 4. Low / High Temperature Alert & Normalization
    float tMin = isPowerOutage ? cfgMgr.config.powerLossTempMin : cfgMgr.config.normalTempMin;
    float tMax = isPowerOutage ? cfgMgr.config.powerLossTempMax : cfgMgr.config.normalTempMax;
    Serial.printf("[Alert] Temperature check: Measured=%.2f C | Safe Band: %.1f - %.1f C\n", measuredTemp, tMin, tMax);

    if (hasFreshData) {
      if (measuredTemp < tMin) {
        Serial.printf("[Alert] ❄️ Under-temperature detected (%.2f < %.1f C)!\n", measuredTemp, tMin);
        if (currentTempAlarmState != STATE_TEMP_ALARM_LOW || (now - lastLimitAlertTime >= (unsigned long)cfgMgr.config.limitAlertIntervalMin * 60000UL)) {
          currentTempAlarmState = STATE_TEMP_ALARM_LOW;
          lastLimitAlertTime = now;
          sendTelegramMessage("❄️ LOW TEMPERATURE ALERT (FREEZE RISK)!\nDevice: " + cfgMgr.config.bleTargetName +
                              "\nMeasured: " + String(measuredTemp, 2) + " °C" +
                              "\nLower Limit: " + String(tMin, 1) + " °C" +
                              "\nDelta: " + String(measuredTemp - tMin, 2) + " °C below limit!" +
                              "\nPower: " + String(isPowerOutage ? "OUTAGE (Battery)" : "ONLINE"));
        } else {
          Serial.println("[Alert] Alert throttled (interval cooldown).");
        }
      } else if (measuredTemp > tMax) {
        Serial.printf("[Alert] 🔥 Over-temperature detected (%.2f > %.1f C)!\n", measuredTemp, tMax);
        if (currentTempAlarmState != STATE_TEMP_ALARM_HIGH || (now - lastLimitAlertTime >= (unsigned long)cfgMgr.config.limitAlertIntervalMin * 60000UL)) {
          currentTempAlarmState = STATE_TEMP_ALARM_HIGH;
          lastLimitAlertTime = now;
          sendTelegramMessage("🔥 HIGH TEMPERATURE ALERT (WARMTH BREACH)!\nDevice: " + cfgMgr.config.bleTargetName +
                              "\nMeasured: " + String(measuredTemp, 2) + " °C" +
                              "\nUpper Limit: " + String(tMax, 1) + " °C" +
                              "\nDelta: +" + String(measuredTemp - tMax, 2) + " °C above limit!" +
                              "\nPower: " + String(isPowerOutage ? "OUTAGE (Battery)" : "ONLINE"));
        } else {
          Serial.println("[Alert] Alert throttled (interval cooldown).");
        }
      } else {
        if (currentTempAlarmState != STATE_TEMP_NORMAL) {
          currentTempAlarmState = STATE_TEMP_NORMAL;
          Serial.println("[Alert] ✅ Temperature normalized back to safe band.");
          sendTelegramMessage("✅ TEMPERATURE NORMALIZED!\nDevice: " + cfgMgr.config.bleTargetName +
                              "\nTemperature returned to safe zone: " + String(measuredTemp, 2) + " °C" +
                              "\nSafe Band: " + String(tMin, 1) + " - " + String(tMax, 1) + " °C");
        } else {
          Serial.println("[Alert] Temperature is within normal bounds. No limit alerts needed.");
        }
      }

      // 5. Rapid Temperature Rise / Drop (Ani Sıcaklık Değişimi)
      if (lastSlopeTemp > -900.0f) {
        float deltaT = measuredTemp - lastSlopeTemp;
        if (fabs(deltaT) >= 1.5f && (now - lastRapidSlopeAlertTime >= 600000UL)) {
          lastRapidSlopeAlertTime = now;
          if (deltaT > 0) {
            Serial.printf("[Alert] 📈 Rapid temperature spike detected (+%.2f C)!\n", deltaT);
            sendTelegramMessage("📈 RAPID TEMPERATURE RISE DETECTED!\nDevice: " + cfgMgr.config.bleTargetName +
                                "\nSudden Jump: +" + String(deltaT, 2) + " °C" +
                                "\nPrevious: " + String(lastSlopeTemp, 2) + " °C -> Current: " + String(measuredTemp, 2) + " °C" +
                                "\n⚠️ Possible door open or cooling system fault!");
          } else {
            Serial.printf("[Alert] 📉 Rapid temperature plunge detected (%.2f C)!\n", deltaT);
            sendTelegramMessage("📉 RAPID TEMPERATURE DROP DETECTED!\nDevice: " + cfgMgr.config.bleTargetName +
                                "\nSudden Drop: " + String(deltaT, 2) + " °C" +
                                "\nPrevious: " + String(lastSlopeTemp, 2) + " °C -> Current: " + String(measuredTemp, 2) + " °C");
          }
        }
      }
      lastSlopeTemp = measuredTemp;
    }

    // --- 3. TELEGRAM BOT INCOMING COMMAND POLLING ---
    if (!checkMenuAbort()) {
      processTelegramIncomingMessages();
    }
  } else {
    Serial.println("[CYCLE] ❌ Wi-Fi connection could not be established. Transmission skipped.");
  }

  Serial.println("[CYCLE] Disconnecting Wi-Fi and returning to low-power BLE mode...");
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  wifiConnectedStatus = false;
  Serial.printf("[CYCLE] Free Heap at end of cycle: %u bytes\n", ESP.getFreeHeap());
  Serial.println("################### [ SEND CYCLE FINISHED ] ###################\n");
}

// --- OLED Display Rendering ---
void drawMenuScreen() {
  u8g2.clearBuffer();
  u8g2.drawRFrame(X_OFFSET, Y_OFFSET, SCREEN_W, SCREEN_H, 2);

  u8g2.setFont(u8g2_font_5x7_tf);
  u8g2.drawStr(X_OFFSET + 12, Y_OFFSET + 8, "CONFIG MENU");

  u8g2.drawStr(X_OFFSET + 4, Y_OFFSET + 18, menuSelection == 0 ? "> 1.WPS Setup" : "  1.WPS Setup");
  u8g2.drawStr(X_OFFSET + 4, Y_OFFSET + 28, menuSelection == 1 ? "> 2.Web Portal" : "  2.Web Portal");
  u8g2.drawStr(X_OFFSET + 4, Y_OFFSET + 38, menuSelection == 2 ? "> 3.Discover" : "  3.Discover");

  u8g2.sendBuffer();
}

void drawNormalScreens() {
  u8g2.clearBuffer();
  u8g2.drawRFrame(X_OFFSET, Y_OFFSET, SCREEN_W, SCREEN_H, 2);

  // 1. Progress Bar (when held for more than 1.5s)
  if (btnIsPressed && sysMode == MODE_NORMAL_RUN) {
    unsigned long heldTime = millis() - btnPressStart;
    if (heldTime >= 1500) {
      int progressW = map(constrain(heldTime, 1500, 5000), 1500, 5000, 0, SCREEN_W - 8);
      u8g2.setFont(u8g2_font_5x7_tf);
      u8g2.drawStr(X_OFFSET + 8, Y_OFFSET + 16, "Opening Menu..");
      u8g2.drawFrame(X_OFFSET + 4, Y_OFFSET + 22, SCREEN_W - 8, 8);
      u8g2.drawBox(X_OFFSET + 4, Y_OFFSET + 22, progressW, 8);
      u8g2.sendBuffer();
      return;
    }
  }

  // 2. 10s Inactivity -> Return to Main Screen & exit browsing mode
  if (isBrowsingScreens && (millis() - lastScreenSwitchTime >= 10000)) {
    isBrowsingScreens = false;
    currentInfoScr = SCR_MAIN_TEMP;
  }

  // 3. Error Pop-up (Only shown on Main Screen when user is not browsing)
  unsigned long timeoutMs = (unsigned long)cfgMgr.config.stageTimeoutSec * 1000UL;
  if (millis() - lastBlePacketReceivedTime >= timeoutMs) {
    bleConnectedStatus = false;
  }

  bool hasError = (!lastWiFiSuccess || !bleConnectedStatus || (!isTimeSynced && ntpFailedWarning));
  if (!isBrowsingScreens && hasError && currentInfoScr == SCR_MAIN_TEMP) {
    unsigned long popCycle = millis() % 6000;
    if (popCycle >= 3500) {
      u8g2.setFont(u8g2_font_5x7_tf);
      if (!bleConnectedStatus) {
        u8g2.drawStr(X_OFFSET + 6, Y_OFFSET + 16, "! WARNING !");
        u8g2.drawStr(X_OFFSET + 6, Y_OFFSET + 28, "Thermometer");
        u8g2.drawStr(X_OFFSET + 6, Y_OFFSET + 37, "No Signal");
      } else if (!lastWiFiSuccess) {
        u8g2.drawStr(X_OFFSET + 6, Y_OFFSET + 16, "! WARNING !");
        u8g2.drawStr(X_OFFSET + 6, Y_OFFSET + 28, "Wi-Fi");
        u8g2.drawStr(X_OFFSET + 6, Y_OFFSET + 37, "Disconnected");
      } else if (!isTimeSynced) {
        u8g2.drawStr(X_OFFSET + 6, Y_OFFSET + 16, "! WARNING !");
        u8g2.drawStr(X_OFFSET + 6, Y_OFFSET + 28, "NTP Time Sync");
        u8g2.drawStr(X_OFFSET + 6, Y_OFFSET + 37, "Failed / Retrying");
      }
      u8g2.sendBuffer();
      return;
    }
  }

  // --- INFO SCREENS ---
  switch (currentInfoScr) {
    case SCR_MAIN_TEMP: {
      u8g2.setFont(u8g2_font_5x7_tf);
      String dName = (cfgMgr.config.bleTargetName.length() > 0) ? cfgMgr.config.bleTargetName : measuredDeviceName;
      u8g2.drawStr(X_OFFSET + 4, Y_OFFSET + 8, dName.c_str());

      String stat = "w:" + String(lastWiFiSuccess ? "V" : "X") + " b:" + String(bleConnectedStatus ? "V" : "X");
      if (!isTimeSynced) stat += " !T";
      u8g2.setFont(u8g2_font_4x6_tf);
      u8g2.drawStr(X_OFFSET + 32, Y_OFFSET + 8, stat.c_str());

      if (everReceivedAnyData) {
        char str[12];
        sprintf(str, "%.1f\xb0", lastDispTemp);
        u8g2.setFont(u8g2_font_logisoso16_tf);
        u8g2.drawStr(X_OFFSET + 4, Y_OFFSET + 32, str);
      } else {
        u8g2.setFont(u8g2_font_6x10_tf);
        u8g2.drawStr(X_OFFSET + 6, Y_OFFSET + 26, "Scanning..");
      }
      break;
    }

    case SCR_HUMIDITY: {
      u8g2.setFont(u8g2_font_5x7_tf);
      u8g2.drawStr(X_OFFSET + 6, Y_OFFSET + 9, "HUMIDITY");
      if (everReceivedAnyData) {
        char str[10];
        sprintf(str, "%%%d", (int)lastDispHum);
        u8g2.setFont(u8g2_font_logisoso16_tf);
        u8g2.drawStr(X_OFFSET + 6, Y_OFFSET + 32, str);
        u8g2.setFont(u8g2_font_5x7_tf);
        u8g2.drawStr(X_OFFSET + 50, Y_OFFSET + 32, "RH");
      }
      break;
    }

    case SCR_BATTERY: {
      u8g2.setFont(u8g2_font_5x7_tf);
      u8g2.drawStr(X_OFFSET + 6, Y_OFFSET + 9, "BATTERY");
      if (everReceivedAnyData) {
        char str[10];
        sprintf(str, "%%%d", lastDispBattery);
        u8g2.setFont(u8g2_font_logisoso16_tf);
        u8g2.drawStr(X_OFFSET + 6, Y_OFFSET + 32, str);

        if (lastDispVoltage > 0.0) {
          char vStr[10];
          sprintf(vStr, "%.2fV", lastDispVoltage);
          u8g2.setFont(u8g2_font_5x7_tf);
          u8g2.drawStr(X_OFFSET + 42, Y_OFFSET + 32, vStr);
        }
      }
      break;
    }

    case SCR_RSSI: {
      u8g2.setFont(u8g2_font_5x7_tf);
      u8g2.drawStr(X_OFFSET + 6, Y_OFFSET + 9, "BLE SIGNAL");
      if (everReceivedAnyData) {
        char str[12];
        sprintf(str, "%d", lastDispRssi);
        u8g2.setFont(u8g2_font_logisoso16_tf);
        u8g2.drawStr(X_OFFSET + 6, Y_OFFSET + 32, str);
        u8g2.setFont(u8g2_font_5x7_tf);
        u8g2.drawStr(X_OFFSET + 48, Y_OFFSET + 32, "dBm");
      }
      break;
    }

    case SCR_MIN_TEMP: {
      float minT, maxT;
      int minDurH, maxDurH;
      get30dMinMax(minT, minDurH, maxT, maxDurH);

      u8g2.setFont(u8g2_font_5x7_tf);
      u8g2.drawStr(X_OFFSET + 4, Y_OFFSET + 8, "30d MINIMUM");

      char tStr[12];
      sprintf(tStr, "%+.1f\xb0", minT);
      u8g2.setFont(u8g2_font_logisoso16_tf);
      u8g2.drawStr(X_OFFSET + 4, Y_OFFSET + 27, tStr);

      char durStr[16];
      if (minDurH >= 24) {
        sprintf(durStr, "%.1fd (+-0.5)", minDurH / 24.0);
      } else {
        sprintf(durStr, "%dh (+-0.5)", minDurH);
      }
      u8g2.setFont(u8g2_font_4x6_tf);
      u8g2.drawStr(X_OFFSET + 4, Y_OFFSET + 37, durStr);
      break;
    }

    case SCR_MAX_TEMP: {
      float minT, maxT;
      int minDurH, maxDurH;
      get30dMinMax(minT, minDurH, maxT, maxDurH);

      u8g2.setFont(u8g2_font_5x7_tf);
      u8g2.drawStr(X_OFFSET + 4, Y_OFFSET + 8, "30d MAXIMUM");

      char tStr[12];
      sprintf(tStr, "%+.1f\xb0", maxT);
      u8g2.setFont(u8g2_font_logisoso16_tf);
      u8g2.drawStr(X_OFFSET + 4, Y_OFFSET + 27, tStr);

      char durStr[16];
      if (maxDurH >= 24) {
        sprintf(durStr, "%.1fd (+-0.5)", maxDurH / 24.0);
      } else {
        sprintf(durStr, "%dh (+-0.5)", maxDurH);
      }
      u8g2.setFont(u8g2_font_4x6_tf);
      u8g2.drawStr(X_OFFSET + 4, Y_OFFSET + 37, durStr);
      break;
    }

    case SCR_WIFI_INFO: {
      u8g2.setFont(u8g2_font_5x7_tf);
      u8g2.drawStr(X_OFFSET + 4, Y_OFFSET + 8, "WIFI INFO");
      u8g2.setFont(u8g2_font_4x6_tf);
      u8g2.drawStr(X_OFFSET + 4, Y_OFFSET + 18, ("SSID: " + cfgMgr.config.wifiSsid).c_str());

      String stateStr = "No Conn";
      if (WiFi.status() == WL_CONNECTED) {
        stateStr = "Connected";
      } else if (lastWiFiSuccess) {
        stateStr = "Standby (OK)";
      } else {
        stateStr = "Disconnected";
      }
      u8g2.drawStr(X_OFFSET + 4, Y_OFFSET + 28, ("State: " + stateStr).c_str());

      String ipStr = "-";
      if (WiFi.status() == WL_CONNECTED && WiFi.localIP().toString() != "0.0.0.0") {
        ipStr = WiFi.localIP().toString();
      } else if (lastAssignedIp != "-" && lastAssignedIp != "0.0.0.0") {
        ipStr = lastAssignedIp;
      }
      u8g2.drawStr(X_OFFSET + 4, Y_OFFSET + 37, ("IP: " + ipStr).c_str());
      break;
    }

    case SCR_BLE_INFO: {
      u8g2.setFont(u8g2_font_5x7_tf);
      u8g2.drawStr(X_OFFSET + 4, Y_OFFSET + 8, "BLE SENSOR INFO");
      u8g2.setFont(u8g2_font_4x6_tf);
      String dName = (cfgMgr.config.bleTargetName.length() > 0) ? cfgMgr.config.bleTargetName : measuredDeviceName;
      u8g2.drawStr(X_OFFSET + 4, Y_OFFSET + 18, ("Name: " + dName).c_str());
      String mac = (cfgMgr.config.bleTargetMac.length() > 0) ? cfgMgr.config.bleTargetMac : measuredMacAddress;
      u8g2.drawStr(X_OFFSET + 4, Y_OFFSET + 28, ("MAC: " + mac).c_str());
      u8g2.drawStr(X_OFFSET + 4, Y_OFFSET + 37, ("RSSI: " + String(lastDispRssi) + " dBm").c_str());
      break;
    }
  }

  u8g2.sendBuffer();
}

void setup() {
  Serial.begin(115200);
  delay(500);

  for (int i = 0; i < HISTORY_SIZE; i++) {
    tempHistory[i].timestamp = 0;
    tempHistory[i].temp = -9999;
  }

  // Initialize LittleFS for persistent 30-day analytics
  if (!LittleFS.begin(true)) {
    Serial.println("[LittleFS] Mount Failed!");
  } else {
    Serial.println("[LittleFS] Mounted successfully. Loading history...");
    loadHistoryFromFS();
  }

  pinMode(BOOT_BTN_PIN, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(BOOT_BTN_PIN), isrButtonChange, CHANGE);

  Wire.begin(5, 6);
  u8g2.begin();
  u8g2.setContrast(255);

  u8g2.clearBuffer();
  u8g2.drawRFrame(X_OFFSET, Y_OFFSET, SCREEN_W, SCREEN_H, 2);
  u8g2.setFont(u8g2_font_6x10_tf);
  u8g2.drawStr(X_OFFSET + 8, Y_OFFSET + 24, "Booting..");
  u8g2.sendBuffer();

  cfgMgr.load();
  pinMode(cfgMgr.config.powerDetectPin, INPUT_PULLUP);

  stateStageStartTime = millis();
  lastScreenSwitchTime = millis();

  startBLEScanForMode();
}

void drawPortalScreen() {
  u8g2.clearBuffer();
  u8g2.drawRFrame(X_OFFSET, Y_OFFSET, SCREEN_W, SCREEN_H, 2);
  u8g2.setFont(u8g2_font_6x10_tf);
  
  int w1 = u8g2.getStrWidth("Thermo_Obs");
  u8g2.drawStr(X_OFFSET + (SCREEN_W - w1) / 2, Y_OFFSET + 12, "Thermo_Obs");

  int w2 = u8g2.getStrWidth(cfgMgr.config.apPassword.c_str());
  u8g2.drawStr(X_OFFSET + (SCREEN_W - w2) / 2, Y_OFFSET + 24, cfgMgr.config.apPassword.c_str());

  u8g2.setFont(u8g2_font_5x7_tf);
  int stations = WiFi.softAPgetStationNum();
  char buf[32];
  if (stations > 0) {
    snprintf(buf, sizeof(buf), "1 Client Connected");
  } else {
    unsigned long elapsedSec = (millis() - portal.getApStartTime()) / 1000UL;
    long remSec = 600 - elapsedSec;
    if (remSec < 0) remSec = 0;
    snprintf(buf, sizeof(buf), "192.168.4.1 (%dm%02ds)", (int)(remSec / 60), (int)(remSec % 60));
  }
  int w3 = u8g2.getStrWidth(buf);
  u8g2.drawStr(X_OFFSET + (SCREEN_W - w3) / 2, Y_OFFSET + 35, buf);

  u8g2.sendBuffer();
}

void handleButtonState() {
  static bool wasPressed = false;
  static unsigned long localPressStart = 0;

  bool pressed = (digitalRead(BOOT_BTN_PIN) == LOW);

  if (pressed && !wasPressed) {
    localPressStart = millis();
    wasPressed = true;
  }
  else if (pressed && wasPressed) {
    unsigned long duration = millis() - localPressStart;
    if (sysMode == MODE_NORMAL_RUN && duration >= 5000) {
      sysMode = MODE_MENU;
      menuSelection = 0;
      menuLastActionTime = millis();
      wasPressed = false;
      isBrowsingScreens = false;
      Serial.println("\n[MENU] 5s Long Press -> Opening Menu!");
    }
    else if (sysMode == MODE_MENU && duration >= 2000) {
      wasPressed = false;
      if (menuSelection == 0) {
        sysMode = MODE_WPS;
        startWPSProcess();
      } else if (menuSelection == 1) {
        sysMode = MODE_WIFI_PORTAL;
        stopBLEAndFreeMem();
        portal.start();
        drawPortalScreen();
      } else if (menuSelection == 2) {
        sysMode = MODE_MANUAL_BLE_DISCOVERY;
        cfgMgr.clearBLE();
        discoveryBestRssi = -999;
        foundCandidate = false;
        stateStageStartTime = millis();
        startBLEScanForMode();
      }
    }
  }
  else if (!pressed && wasPressed) {
    unsigned long duration = millis() - localPressStart;
    wasPressed = false;

    // Short Press (50ms - 1500ms): Instant screen switching & pause background scan
    if (sysMode == MODE_NORMAL_RUN && duration < 1500 && duration > 50) {
      isBrowsingScreens = true;
      stopBLE(); // Pause BLE scan while browsing to eliminate lag and flicker
      currentInfoScr = (InfoScreen)((currentInfoScr + 1) % SCR_TOTAL_COUNT);
      lastScreenSwitchTime = millis();
      Serial.printf("[SCREEN] Switched to: %d\n", (int)currentInfoScr);
    }
    // Short press while in Menu mode
    else if (sysMode == MODE_MENU && duration < 2000 && duration > 50) {
      menuSelection = (menuSelection + 1) % 3;
      menuLastActionTime = millis();
    }
  }
}

void loop() {
  handleButtonState();

  int rawPower = digitalRead(cfgMgr.config.powerDetectPin);
  isPowerOutage = (cfgMgr.config.powerPinActiveLow == 1) ? (rawPower == LOW) : (rawPower == HIGH);

  // 1) Add sample to RAM buffer immediately on first BLE packet, then every 5 minutes (300,000 ms)
  if (everReceivedAnyData && (lastHistorySampleTime == 0 || millis() - lastHistorySampleTime >= 300000UL)) {
    lastHistorySampleTime = millis();
    addTempSample(lastDispTemp);
  }

  // 2) Save RAM buffer to LittleFS Flash every 30 minutes (1,800,000 ms)
  if (historyCount > 0 && (millis() - lastHistorySaveTime >= 1800000UL)) {
    lastHistorySaveTime = millis();
    saveHistoryToFS();
  }

  // --- MOD 1: MENU ---
  if (sysMode == MODE_MENU) {
    drawMenuScreen();
    if (millis() - menuLastActionTime >= 10000) {
      Serial.println("[MENU] Inactivity -> Soft Reset...");
      if (historyCount > 0) saveHistoryToFS();
      u8g2.clearBuffer();
      u8g2.drawStr(X_OFFSET + 10, Y_OFFSET + 24, "Restarting.");
      u8g2.sendBuffer();
      delay(500);
      ESP.restart();
    }
    return;
  }

  // --- MOD 2: WPS ---
  if (sysMode == MODE_WPS) {
    u8g2.clearBuffer();
    u8g2.drawRFrame(X_OFFSET, Y_OFFSET, SCREEN_W, SCREEN_H, 2);
    u8g2.setFont(u8g2_font_5x7_tf);
    u8g2.drawStr(X_OFFSET + 6, Y_OFFSET + 16, "WPS MODE:");
    u8g2.drawStr(X_OFFSET + 6, Y_OFFSET + 28, "Press Router WPS");
    u8g2.sendBuffer();

    if (wpsSuccess) {
      cfgMgr.config.wifiSsid = WiFi.SSID();
      cfgMgr.config.wifiPass = WiFi.psk();
      cfgMgr.save();

      u8g2.clearBuffer();
      u8g2.drawStr(X_OFFSET + 6, Y_OFFSET + 24, "WPS SUCCESS!");
      u8g2.sendBuffer();
      delay(2000);
      ESP.restart();
    }
    return;
  }

  // --- MOD 3: WEB PORTAL ---
  if (sysMode == MODE_WIFI_PORTAL) {
    portal.handle();

    static unsigned long lastPortalOled = 0;
    if (millis() - lastPortalOled >= 1000) {
      lastPortalOled = millis();
      drawPortalScreen();
    }

    delay(2); // Yield CPU to Wi-Fi driver, DHCP server, and FreeRTOS tasks!
    return;
  }

  // --- MOD 4: NORMAL / DISCOVERY ---
  drawNormalScreens();

  // PAUSE BACKGROUND SCANNING AND UPLOADING WHILE USER IS BROWSING SCREENS!
  if (isBrowsingScreens) {
    return;
  }

  unsigned long now = millis();
  unsigned long timeoutMs = (unsigned long)cfgMgr.config.stageTimeoutSec * 1000UL;
  unsigned long bleIntervalMs = (unsigned long)cfgMgr.config.bleReadIntervalSec * 1000UL;

  if (appState == STATE_SCAN_BLE) {
    if (pBLEScan == nullptr) {
      startBLEScanForMode();
    }
    pBLEScan->start(1, false);
    pBLEScan->clearResults();

    if (checkMenuAbort()) return;

    if (hasFreshData) {
      Serial.printf("\n[STATUS] Fresh BLE Sample (%.2f C) -> Sending via Wi-Fi\n", measuredTemp);
      appState = STATE_SEND_WIFI;
      stateStageStartTime = now;
    }
    else if ((cfgMgr.config.bleTargetMac.length() == 0 || sysMode == MODE_MANUAL_BLE_DISCOVERY) && foundCandidate && (now - stateStageStartTime >= 10000)) {
      Serial.printf("\n[DISCOVERY] Best Device Selected: %s (%s RSSI: %d dBm)\n",
                    candidateName.c_str(), candidateMac.c_str(), discoveryBestRssi);
      cfgMgr.config.bleTargetMac = candidateMac;
      cfgMgr.config.bleTargetName = candidateName;
      cfgMgr.save();

      measuredTemp = cfgMgr.applyCalibration(candidateTemp);
      measuredHum = candidateHum;
      measuredBattery = candidateBattery;
      measuredVoltage = candidateVoltage;
      measuredRssi = discoveryBestRssi;
      measuredDeviceName = candidateName;
      measuredMacAddress = candidateMac;
      hasFreshData = true;

      lastDispTemp = measuredTemp;
      lastDispHum = candidateHum;
      lastDispBattery = candidateBattery;
      lastDispVoltage = candidateVoltage;
      lastDispRssi = discoveryBestRssi;
      everReceivedAnyData = true;
      bleConnectedStatus = true;
      lastBlePacketReceivedTime = millis();

      sysMode = MODE_NORMAL_RUN;
      appState = STATE_SEND_WIFI;
      stateStageStartTime = now;
    }
    else if (now - stateStageStartTime >= timeoutMs) {
      if (cfgMgr.config.bleTargetMac.length() > 0 && sysMode == MODE_NORMAL_RUN) {
        Serial.printf("\n[WARN] %d s BLE Timeout -> Triggering Auto-Discovery...\n", cfgMgr.config.stageTimeoutSec);
        cfgMgr.clearBLE();
        discoveryBestRssi = -999;
        foundCandidate = false;
        bleConnectedStatus = false;
        stateStageStartTime = now;
      } else {
        Serial.printf("\n[WARN] No thermometer found in %d s -> Forwarding empty payload via Wi-Fi\n", cfgMgr.config.stageTimeoutSec);
        hasFreshData = false;
        bleConnectedStatus = false;
        appState = STATE_SEND_WIFI;
        stateStageStartTime = now;
      }
    }
  }
  else if (appState == STATE_SEND_WIFI) {
    executeSendCycle();

    hasFreshData = false;
    waitIntervalStartTime = millis();
    appState = STATE_WAIT_INTERVAL;
  }
  else if (appState == STATE_WAIT_INTERVAL) {
    // Non-blocking wait (millis-based)
    // Button handling and OLED rendering continue uninterrupted in loop.
    if (now - waitIntervalStartTime >= bleIntervalMs) {
      appState = STATE_SCAN_BLE;
      stateStageStartTime = millis();
      startBLEScanForMode();
    }
  }
}