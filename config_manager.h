#pragma once
#include <Arduino.h>
#include <Preferences.h>

struct AppConfig {
  // AP Portal Password (Persistent NVS)
  String apPassword;

  // Wi-Fi (Primary & Backup)
  String wifiSsid;
  String wifiPass;
  String backupWifiSsid;
  String backupWifiPass;

  // BLE Thermometer
  String bleTargetMac;
  String bleTargetName;

  // Timing & Intervals (in seconds)
  int bleReadIntervalSec;    // BLE reading/send interval in seconds (default: 60)
  int stageTimeoutSec;       // Timeout for discovery/waiting in seconds (default: 185)

  // Logging & Limits
  int logIntervalMin;        // e.g. 1 min
  float normalTempMin;       // e.g. 2.0 °C
  float normalTempMax;       // e.g. 8.0 °C
  int limitAlertIntervalMin; // Limit alert repeat interval (min)

  // Power Loss Detection
  int powerDetectPin;        // e.g. GPIO 4
  int powerPinActiveLow;     // 1: LOW = Power lost, 0: HIGH = Power lost
  float powerLossTempMin;    // Min temp during power loss (e.g. 1.0 °C)
  float powerLossTempMax;    // Max temp during power loss (e.g. 10.0 °C)
  int powerLossAlertIntervalMin; // Power loss alert interval (min)
  bool notifyTelegramOnPowerLoss;
  bool notifyWebhookOnPowerLoss;
  bool notifySheetsOnPowerLoss;

  // Notification Endpoints (Placeholders for public repo)
  String googleScriptUrl;
  String webhookUrl;
  String telegramBotToken;
  String telegramChatId;

  // 4-Point Laboratory Temperature Calibration (2.0°C, 4.0°C, 6.0°C, 8.0°C)
  float calRaw2;         // Raw sensor reading when master reference is 2.0 °C
  float calRaw4;         // Raw sensor reading when master reference is 4.0 °C
  float calRaw6;         // Raw sensor reading when master reference is 6.0 °C
  float calRaw8;         // Raw sensor reading when master reference is 8.0 °C
  String calDate;        // Calibration date & time string
  String calPassword;    // Optional calibration security password (empty = unlocked)
  float calStdDev;       // Computed standard deviation across the 4 points
};

class ConfigManager {
private:
  Preferences prefs;

public:
  AppConfig config;

  String generateRandomPassword() {
    String chars = "0123456789";
    String pass = "";
    for (int i = 0; i < 8; i++) {
      pass += chars.charAt(random(0, chars.length()));
    }
    return pass;
  }

  void load() {
    prefs.begin("tracker_cfg", true);

    config.apPassword = prefs.getString("ap_pass", "");
    if (config.apPassword.length() < 8) {
      prefs.end();
      config.apPassword = generateRandomPassword();
      prefs.begin("tracker_cfg", false);
      prefs.putString("ap_pass", config.apPassword);
      prefs.end();
      prefs.begin("tracker_cfg", true);
    }

    config.wifiSsid = prefs.getString("ssid", "YOUR_WIFI_SSID");
    config.wifiPass = prefs.getString("pass", "YOUR_WIFI_PASSWORD");
    config.backupWifiSsid = prefs.getString("b_ssid", "");
    config.backupWifiPass = prefs.getString("b_pass", "");

    config.bleTargetMac = prefs.getString("ble_mac", "");
    config.bleTargetName = prefs.getString("ble_name", "");

    config.bleReadIntervalSec = prefs.getInt("ble_int", 60);
    if (config.bleReadIntervalSec < 5) config.bleReadIntervalSec = 5;

    config.stageTimeoutSec = prefs.getInt("stg_tout", 185);
    if (config.stageTimeoutSec < 10) config.stageTimeoutSec = 10;

    config.logIntervalMin = prefs.getInt("log_int", 1);
    config.normalTempMin = prefs.getFloat("t_min", 2.0);
    config.normalTempMax = prefs.getFloat("t_max", 8.0);
    config.limitAlertIntervalMin = prefs.getInt("lim_int", 15);

    config.powerDetectPin = prefs.getInt("pwr_pin", 4);
    config.powerPinActiveLow = prefs.getInt("pwr_low", 1);
    config.powerLossTempMin = prefs.getFloat("pwr_t_min", 1.0);
    config.powerLossTempMax = prefs.getFloat("pwr_t_max", 10.0);
    config.powerLossAlertIntervalMin = prefs.getInt("pwr_int", 10);
    config.notifyTelegramOnPowerLoss = prefs.getBool("pwr_tg", true);
    config.notifyWebhookOnPowerLoss = prefs.getBool("pwr_wh", true);
    config.notifySheetsOnPowerLoss = prefs.getBool("pwr_gs", true);

    config.googleScriptUrl = prefs.getString("gs_url", "https://script.google.com/macros/s/YOUR_SCRIPT_ID/exec");
    config.webhookUrl = prefs.getString("wh_url", "");
    config.telegramBotToken = prefs.getString("tg_token", "");
    config.telegramChatId = prefs.getString("tg_chat", "");

    // 4-Point Calibration
    config.calRaw2 = prefs.getFloat("cal_r2", 2.0f);
    config.calRaw4 = prefs.getFloat("cal_r4", 4.0f);
    config.calRaw6 = prefs.getFloat("cal_r6", 6.0f);
    config.calRaw8 = prefs.getFloat("cal_r8", 8.0f);
    config.calDate = prefs.getString("cal_date", "Factory Default (2, 4, 6, 8)");
    config.calPassword = prefs.getString("cal_pass", "");
    config.calStdDev = prefs.getFloat("cal_sd", 0.0f);

    prefs.end();
  }

  float calculateStdDev() {
    float d1 = config.calRaw2 - 2.0f;
    float d2 = config.calRaw4 - 4.0f;
    float d3 = config.calRaw6 - 6.0f;
    float d4 = config.calRaw8 - 8.0f;
    float mean = (d1 + d2 + d3 + d4) / 4.0f;
    float variance = ((d1 - mean) * (d1 - mean) +
                      (d2 - mean) * (d2 - mean) +
                      (d3 - mean) * (d3 - mean) +
                      (d4 - mean) * (d4 - mean)) / 3.0f;
    if (variance < 0.0f) variance = 0.0f;
    return sqrt(variance);
  }

  float applyCalibration(float rawT) {
    float r2 = config.calRaw2;
    float r4 = config.calRaw4;
    float r6 = config.calRaw6;
    float r8 = config.calRaw8;

    // Guard against identical or inverted points
    if (r4 <= r2) r4 = r2 + 2.0f;
    if (r6 <= r4) r6 = r4 + 2.0f;
    if (r8 <= r6) r8 = r6 + 2.0f;

    // Segment 1: rawT <= r2
    if (rawT <= r2) {
      float slope = (4.0f - 2.0f) / (r4 - r2);
      return 2.0f + slope * (rawT - r2);
    }
    // Segment 2: r2 < rawT <= r4
    else if (rawT <= r4) {
      return 2.0f + ((4.0f - 2.0f) / (r4 - r2)) * (rawT - r2);
    }
    // Segment 3: r4 < rawT <= r6
    else if (rawT <= r6) {
      return 4.0f + ((6.0f - 4.0f) / (r6 - r4)) * (rawT - r4);
    }
    // Segment 4: r6 < rawT <= r8
    else if (rawT <= r8) {
      return 6.0f + ((8.0f - 6.0f) / (r8 - r6)) * (rawT - r6);
    }
    // Segment 5: rawT > r8
    else {
      float slope = (8.0f - 6.0f) / (r8 - r6);
      return 8.0f + slope * (rawT - r8);
    }
  }

  void save() {
    prefs.begin("tracker_cfg", false);

    prefs.putString("ap_pass", config.apPassword);
    prefs.putString("ssid", config.wifiSsid);
    prefs.putString("pass", config.wifiPass);
    prefs.putString("b_ssid", config.backupWifiSsid);
    prefs.putString("b_pass", config.backupWifiPass);

    prefs.putString("ble_mac", config.bleTargetMac);
    prefs.putString("ble_name", config.bleTargetName);

    prefs.putInt("ble_int", config.bleReadIntervalSec);
    prefs.putInt("stg_tout", config.stageTimeoutSec);

    prefs.putInt("log_int", config.logIntervalMin);
    prefs.putFloat("t_min", config.normalTempMin);
    prefs.putFloat("t_max", config.normalTempMax);
    prefs.putInt("lim_int", config.limitAlertIntervalMin);

    prefs.putInt("pwr_pin", config.powerDetectPin);
    prefs.putInt("pwr_low", config.powerPinActiveLow);
    prefs.putFloat("pwr_t_min", config.powerLossTempMin);
    prefs.putFloat("pwr_t_max", config.powerLossTempMax);
    prefs.putInt("pwr_int", config.powerLossAlertIntervalMin);
    prefs.putBool("pwr_tg", config.notifyTelegramOnPowerLoss);
    prefs.putBool("pwr_wh", config.notifyWebhookOnPowerLoss);
    prefs.putBool("pwr_gs", config.notifySheetsOnPowerLoss);

    prefs.putString("gs_url", config.googleScriptUrl);
    prefs.putString("wh_url", config.webhookUrl);
    prefs.putString("tg_token", config.telegramBotToken);
    prefs.putString("tg_chat", config.telegramChatId);

    // 4-Point Calibration
    config.calStdDev = calculateStdDev();
    prefs.putFloat("cal_r2", config.calRaw2);
    prefs.putFloat("cal_r4", config.calRaw4);
    prefs.putFloat("cal_r6", config.calRaw6);
    prefs.putFloat("cal_r8", config.calRaw8);
    prefs.putString("cal_date", config.calDate);
    prefs.putString("cal_pass", config.calPassword);
    prefs.putFloat("cal_sd", config.calStdDev);

    prefs.end();
  }

  void clearBLE() {
    prefs.begin("tracker_cfg", false);
    prefs.putString("ble_mac", "");
    prefs.putString("ble_name", "");
    prefs.end();
    config.bleTargetMac = "";
    config.bleTargetName = "";
  }

  void factoryReset() {
    prefs.begin("tracker_cfg", false);
    prefs.clear();
    prefs.end();
    Serial.println("[NVS] Factory Reset Complete (NVS Formatted).");
  }
};