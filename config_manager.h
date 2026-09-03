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

    prefs.end();
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