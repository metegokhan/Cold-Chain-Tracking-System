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

// NTP Time Synchronization status
bool isTimeSynced = false;

void syncNtpTime() {
  Serial.println("[NTP] Syncing network time (GMT+3)...");
  // GMT+3 (Turkey / Europe/Istanbul) = 3 * 3600 = 10800s offset, 0 daylight saving
  configTime(3 * 3600, 0, "pool.ntp.org", "time.google.com", "time.cloudflare.com");
  struct tm timeinfo;
  if (getLocalTime(&timeinfo, 5000)) {
    isTimeSynced = true;
    Serial.printf("[NTP] Time Synced Successfully: %02d.%02d.%04d %02d:%02d:%02d\n",
                  timeinfo.tm_mday, timeinfo.tm_mon + 1, timeinfo.tm_year + 1900,
                  timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
  } else {
    Serial.println("[NTP] Time Sync Failed or timed out.");
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
  WiFi.mode(WIFI_STA);
  WiFi.disconnect(true);

  WiFi.onEvent(WiFiEvent);
  wps_config.wps_type = WPS_TYPE_PBC;

  esp_wifi_wps_enable(&wps_config);
  esp_wifi_wps_start(0);

  Serial.println("\n[WPS] WPS PBC Started. Please press WPS button on your router...");
}

void sendTelegramMessage(String msg) {
  if (cfgMgr.config.telegramBotToken.length() == 0 || cfgMgr.config.telegramChatId.length() == 0) return;

  NetworkClientSecure client;
  client.setInsecure();
  HTTPClient https;

  String url = "https://api.telegram.org/bot" + cfgMgr.config.telegramBotToken +
               "/sendMessage?chat_id=" + cfgMgr.config.telegramChatId +
               "&text=" + urlEncode(msg);

  if (https.begin(client, url)) {
    https.setTimeout(8000);
    int code = https.GET();
    Serial.printf("[Telegram] Response Code: %d\n", code);
    https.end();
  }
}

void sendWebhookMessage(String jsonPayload) {
  if (cfgMgr.config.webhookUrl.length() == 0) return;

  NetworkClientSecure client;
  client.setInsecure();
  HTTPClient https;

  if (https.begin(client, cfgMgr.config.webhookUrl)) {
    https.addHeader("Content-Type", "application/json");
    https.setTimeout(8000);
    https.POST(jsonPayload);
    https.end();
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
  if (cfgMgr.config.wifiSsid.length() > 0 && cfgMgr.config.wifiSsid != "YOUR_WIFI_SSID") {
    Serial.printf("[WIFI] Trying Primary SSID: %s\n", cfgMgr.config.wifiSsid.c_str());
    WiFi.begin(cfgMgr.config.wifiSsid.c_str(), cfgMgr.config.wifiPass.c_str());

    unsigned long start = millis();
    while (WiFi.status() != WL_CONNECTED && (millis() - start < 15000)) {
      delay(50);
      if (checkMenuAbort()) {
        WiFi.disconnect(true);
        wifiConnectedStatus = false;
        return false;
      }
    }

    if (WiFi.status() == WL_CONNECTED) {
      wifiConnectedStatus = true;
      Serial.printf("[WIFI] Connected to Primary! IP: %s\n", WiFi.localIP().toString().c_str());
      if (!isTimeSynced) syncNtpTime();
      return true;
    }
  }

  if (cfgMgr.config.backupWifiSsid.length() > 0) {
    Serial.printf("[WIFI] Trying Backup SSID: %s\n", cfgMgr.config.backupWifiSsid.c_str());
    WiFi.disconnect(true);
    delay(50);
    WiFi.begin(cfgMgr.config.backupWifiSsid.c_str(), cfgMgr.config.backupWifiPass.c_str());

    unsigned long start = millis();
    while (WiFi.status() != WL_CONNECTED && (millis() - start < 15000)) {
      delay(50);
      if (checkMenuAbort()) {
        WiFi.disconnect(true);
        wifiConnectedStatus = false;
        return false;
      }
    }

    if (WiFi.status() == WL_CONNECTED) {
      wifiConnectedStatus = true;
      Serial.printf("[WIFI] Connected to Backup! IP: %s\n", WiFi.localIP().toString().c_str());
      if (!isTimeSynced) syncNtpTime();
      return true;
    }
  }

  wifiConnectedStatus = false;
  return false;
}

void executeSendCycle() {
  stopBLE();
  delay(50);

  WiFi.mode(WIFI_STA);

  if (connectToAvailableWiFi()) {
    if (checkMenuAbort()) { WiFi.disconnect(true); return; }

    if (cfgMgr.config.googleScriptUrl.length() > 0 && cfgMgr.config.googleScriptUrl.indexOf("YOUR_SCRIPT_ID") == -1) {
      NetworkClientSecure client;
      client.setInsecure();
      HTTPClient https;

      String dName = (cfgMgr.config.bleTargetName.length() > 0) ? cfgMgr.config.bleTargetName : measuredDeviceName;
      String url = cfgMgr.config.googleScriptUrl + "?device=" + dName;

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

      if (https.begin(client, url)) {
        https.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
        https.setTimeout(8000);
        int code = https.GET();
        Serial.printf("[Sheets] Response: %d\n", code);
        https.end();
      }
    }

    if (checkMenuAbort()) { WiFi.disconnect(true); return; }

    unsigned long now = millis();
    float tMin = isPowerOutage ? cfgMgr.config.powerLossTempMin : cfgMgr.config.normalTempMin;
    float tMax = isPowerOutage ? cfgMgr.config.powerLossTempMax : cfgMgr.config.normalTempMax;

    if (hasFreshData && (measuredTemp < tMin || measuredTemp > tMax)) {
      if (now - lastLimitAlertTime >= (unsigned long)cfgMgr.config.limitAlertIntervalMin * 60000UL) {
        lastLimitAlertTime = now;
        String alertMsg = "⚠️ TEMPERATURE ALERT!\nDevice: " + cfgMgr.config.bleTargetName +
                          "\nTemp: " + String(measuredTemp, 2) + " °C" +
                          "\nThreshold: " + String(tMin, 1) + " - " + String(tMax, 1) + " °C" +
                          "\nPower: " + String(isPowerOutage ? "OUTAGE" : "ONLINE");
        sendTelegramMessage(alertMsg);
      }
    }

    if (isPowerOutage) {
      if (now - lastPowerAlertTime >= (unsigned long)cfgMgr.config.powerLossAlertIntervalMin * 60000UL) {
        lastPowerAlertTime = now;
        if (cfgMgr.config.notifyTelegramOnPowerLoss) {
          sendTelegramMessage("🚨 POWER OUTAGE ALERT!\nMains electricity disconnected.\nRunning on battery.\nTemp: " + String(measuredTemp, 2) + " °C");
        }
      }
    }
  }

  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
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

  bool hasError = (!wifiConnectedStatus || !bleConnectedStatus);
  if (!isBrowsingScreens && hasError && currentInfoScr == SCR_MAIN_TEMP) {
    unsigned long popCycle = millis() % 5000;
    if (popCycle >= 3000) {
      u8g2.setFont(u8g2_font_5x7_tf);
      if (!bleConnectedStatus) {
        u8g2.drawStr(X_OFFSET + 6, Y_OFFSET + 16, "! WARNING !");
        u8g2.drawStr(X_OFFSET + 6, Y_OFFSET + 28, "Thermometer");
        u8g2.drawStr(X_OFFSET + 6, Y_OFFSET + 37, "No Signal");
      } else if (!wifiConnectedStatus) {
        u8g2.drawStr(X_OFFSET + 6, Y_OFFSET + 16, "! WARNING !");
        u8g2.drawStr(X_OFFSET + 6, Y_OFFSET + 28, "Wi-Fi");
        u8g2.drawStr(X_OFFSET + 6, Y_OFFSET + 37, "Disconnected");
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
      u8g2.setFont(u8g2_font_4x6_tf);
      u8g2.drawStr(X_OFFSET + 40, Y_OFFSET + 8, stat.c_str());

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
        portal.start();
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

    u8g2.clearBuffer();
    u8g2.drawRFrame(X_OFFSET, Y_OFFSET, SCREEN_W, SCREEN_H, 2);
    u8g2.setFont(u8g2_font_6x10_tf);
    
    int w1 = u8g2.getStrWidth("Thermo_Obs");
    u8g2.drawStr(X_OFFSET + (SCREEN_W - w1) / 2, Y_OFFSET + 12, "Thermo_Obs");

    int w2 = u8g2.getStrWidth(cfgMgr.config.apPassword.c_str());
    u8g2.drawStr(X_OFFSET + (SCREEN_W - w2) / 2, Y_OFFSET + 24, cfgMgr.config.apPassword.c_str());

    u8g2.setFont(u8g2_font_5x7_tf);
    int w3 = u8g2.getStrWidth("192.168.4.1");
    u8g2.drawStr(X_OFFSET + (SCREEN_W - w3) / 2, Y_OFFSET + 35, "192.168.4.1");

    u8g2.sendBuffer();
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