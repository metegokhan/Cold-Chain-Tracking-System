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
void startBLEScanForMode();

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

void startBLEScanForMode() {
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);

  if (pBLEScan == nullptr) {
    BLEDevice::init("");
    pBLEScan = BLEDevice::getScan();
    pBLEScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks(), true);
    pBLEScan->setActiveScan(true);
    pBLEScan->setInterval(100);
    pBLEScan->setWindow(99);
  }
}

void stopBLE() {
  if (pBLEScan) {
    pBLEScan->stop();
    pBLEScan->clearResults();
  }
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
  if (cfgMgr.config.telegramBotToken.length() == 0 || cfgMgr.config.telegramChatId.length() == 0) {
    Serial.println("[Telegram] ℹ️ Telegram not configured (Token or Chat ID empty in NVS).");
    return;
  }

  Serial.println("[Telegram] 📤 Dispatching alert to Telegram...");
  NetworkClientSecure client;
  client.setInsecure();
  HTTPClient https;

  String url = "https://api.telegram.org/bot" + cfgMgr.config.telegramBotToken +
               "/sendMessage?chat_id=" + cfgMgr.config.telegramChatId +
               "&text=" + urlEncode(msg);

  if (https.begin(client, url)) {
    https.setTimeout(10000);
    unsigned long tStart = millis();
    int code = https.GET();
    unsigned long dur = millis() - tStart;

    if (code > 0) {
      Serial.printf("[Telegram] ✅ HTTP Response: %d (took %lu ms)\n", code, dur);
      if (code != 200) {
        String resp = https.getString();
        Serial.printf("[Telegram] ⚠️ Telegram API Error Body: %s\n", resp.c_str());
      }
    } else {
      Serial.printf("[Telegram] ❌ Connection failed! Error: %s (code %d, took %lu ms)\n",
                    https.errorToString(code).c_str(), code, dur);
    }
    https.end();
  } else {
    Serial.println("[Telegram] ❌ https.begin() failed! Cannot initialize TLS connection to api.telegram.org");
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
  return false;
}

void executeSendCycle() {
  Serial.println("\n################### [ SEND CYCLE START ] ###################");
  Serial.printf("[CYCLE] Timestamp: %lu ms\n", millis());
  Serial.printf("[CYCLE] Telemetry Data: Temp=%.2f C, Hum=%.1f %%, Bat=%d %%, Volt=%.2f V, RSSI=%d dBm\n",
                measuredTemp, measuredHum, measuredBattery, measuredVoltage, measuredRssi);
  Serial.printf("[CYCLE] Fresh BLE Data: %s | Power: %s\n",
                hasFreshData ? "YES" : "NO", isPowerOutage ? "OUTAGE (Battery)" : "ONLINE (Mains)");
  Serial.printf("[CYCLE] Free Heap before stopping BLE: %u bytes\n", ESP.getFreeHeap());

  stopBLE();
  delay(50);
  Serial.printf("[CYCLE] Free Heap after stopping BLE: %u bytes\n", ESP.getFreeHeap());

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
      Serial.printf("[Sheets] 📡 Connecting via HTTPS... (Free Heap: %u bytes, Min Ever: %u bytes)\n",
                    ESP.getFreeHeap(), ESP.getMinFreeHeap());

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
  } else {
    Serial.println("[CYCLE] ❌ Wi-Fi connection could not be established. Transmission skipped.");
  }

  Serial.println("[CYCLE] Disconnecting Wi-Fi and returning to low-power BLE mode...");
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
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

  bool hasError = (!wifiConnectedStatus || !bleConnectedStatus || (!isTimeSynced && ntpFailedWarning));
  if (!isBrowsingScreens && hasError && currentInfoScr == SCR_MAIN_TEMP) {
    unsigned long popCycle = millis() % 6000;
    if (popCycle >= 3500) {
      u8g2.setFont(u8g2_font_5x7_tf);
      if (!bleConnectedStatus) {
        u8g2.drawStr(X_OFFSET + 6, Y_OFFSET + 16, "! WARNING !");
        u8g2.drawStr(X_OFFSET + 6, Y_OFFSET + 28, "Thermometer");
        u8g2.drawStr(X_OFFSET + 6, Y_OFFSET + 37, "No Signal");
      } else if (!wifiConnectedStatus) {
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

      String stat = "w:" + String(wifiConnectedStatus ? "V" : "X") + " b:" + String(bleConnectedStatus ? "V" : "X");
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
      u8g2.drawStr(X_OFFSET + 4, Y_OFFSET + 28, ("State: " + String(wifiConnectedStatus ? "Connected" : "No Conn")).c_str());
      u8g2.drawStr(X_OFFSET + 4, Y_OFFSET + 37, ("IP: " + (wifiConnectedStatus ? WiFi.localIP().toString() : "-")).c_str());
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
  if (stations > 0) {
    char buf[24];
    snprintf(buf, sizeof(buf), "Clients: %d", stations);
    int w3 = u8g2.getStrWidth(buf);
    u8g2.drawStr(X_OFFSET + (SCREEN_W - w3) / 2, Y_OFFSET + 35, buf);
  } else {
    int w3 = u8g2.getStrWidth("192.168.4.1");
    u8g2.drawStr(X_OFFSET + (SCREEN_W - w3) / 2, Y_OFFSET + 35, "192.168.4.1");
  }

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
        stopBLE();
        BLEDevice::deinit(true); // Releases ~120KB of Bluedroid RAM for Wi-Fi AP & WebServer!
        pBLEScan = nullptr;
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

  // 1) Add sample to RAM buffer every 5 minutes (300,000 ms)
  if (everReceivedAnyData && (millis() - lastHistorySampleTime >= 300000UL)) {
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