#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEScan.h>
#include "config_manager.h"
#include "history_manager.h"
#include "report_generator.h"
#include "about_page.h"

extern ConfigManager cfgMgr;

struct DiscoveredBLEDevice {
  String mac;
  String name;
  int rssi;
  float temp;
  float hum;
  int battery;
};

extern std::vector<DiscoveredBLEDevice> discoveredBLEs;
extern BLEScan* pBLEScan;

class WebPortal {
private:
  WebServer server;
  DNSServer dnsServer;
  bool isRunning = false;

  String buildHtml() {
    String html = "<!DOCTYPE html><html lang=\"en\"><head><meta charset=\"UTF-8\">";
    html += "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">";
    html += "<title>Thermo_Obs Configuration Portal</title>";
    html += "<style>";
    html += "body{font-family:-apple-system,BlinkMacSystemFont,sans-serif;background:#f0f2f5;margin:0;padding:15px;color:#333;}";
    html += ".card{background:#fff;border-radius:12px;padding:20px;margin-bottom:15px;box-shadow:0 2px 8px rgba(0,0,0,0.08);}";
    html += "h2{margin-top:0;color:#1a73e8;font-size:18px;border-bottom:2px solid #e8f0fe;padding-bottom:8px;}";
    html += "label{display:block;font-size:13px;font-weight:600;margin:10px 0 4px;color:#555;}";
    html += "input,select{width:100%;box-sizing:border-box;padding:10px;border:1px solid #ccc;border-radius:6px;font-size:14px;}";
    html += ".row{display:flex;gap:10px;}.col{flex:1;}";
    html += ".btn{background:#1a73e8;color:#fff;border:none;padding:14px;border-radius:8px;font-size:16px;font-weight:600;width:100%;cursor:pointer;margin-top:15px;}";
    html += ".btn-scan{background:#34a853;display:inline-block;text-align:center;text-decoration:none;padding:10px 14px;border-radius:6px;color:#fff;font-weight:600;margin-top:10px;font-size:14px;}";
    html += ".btn-danger{background:#d93025;display:inline-block;text-align:center;text-decoration:none;padding:12px;border-radius:8px;color:#fff;font-weight:600;width:100%;box-sizing:border-box;margin-top:10px;}";
    html += ".device-item{border:1px solid #e0e0e0;border-radius:6px;padding:8px;margin-bottom:8px;display:flex;align-items:center;justify-content:space-between;}";
    html += ".badge{background:#e8f0fe;color:#1a73e8;padding:3px 6px;border-radius:4px;font-size:11px;font-weight:600;}";
    html += "#status-box{display:none;position:fixed;top:20px;left:20px;right:20px;background:#34a853;color:#fff;padding:15px;border-radius:8px;text-align:center;font-weight:bold;z-index:999;}";
    html += "#scan-modal{display:none;position:fixed;top:0;left:0;right:0;bottom:0;background:rgba(0,0,0,0.7);color:#fff;z-index:1000;display:none;flex-direction:column;align-items:center;justify-content:center;}";
    html += ".spinner{border:5px solid #f3f3f3;border-top:5px solid #34a853;border-radius:50%;width:45px;height:45px;animation:spin 1s linear infinite;margin-bottom:15px;}";
    html += "@keyframes spin{0%{transform:rotate(0deg);}100%{transform:rotate(360deg);}}";
    html += "</style>";
    html += "<script>";
    html += "function submitForm(e){";
    html += "  e.preventDefault();";
    html += "  var form = document.getElementById('cfg-form');";
    html += "  var formData = new FormData(form);";
    html += "  var btn = document.getElementById('save-btn');";
    html += "  btn.disabled = true; btn.innerText = 'Saving...';";
    html += "  fetch('/save', {method: 'POST', body: formData})";
    html += "  .then(response => {";
    html += "    document.getElementById('status-box').style.display = 'block';";
    html += "    setTimeout(()=>{ window.location.reload(); }, 3000);";
    html += "  })";
    html += "  .catch(error => { alert('Error occurred!'); btn.disabled = false; btn.innerText = 'SAVE'; });";
    html += "}";
    html += "function triggerScan(){";
    html += "  var m = document.getElementById('scan-modal');";
    html += "  m.style.display = 'flex';";
    html += "  fetch('/trigger_scan').then(r => r.text()).then(d => { window.location.href = '/'; });";
    html += "}";
    html += "function updateReportUrls(range){";
    html += "  var rep = document.getElementById('btn-report');";
    html += "  var csv = document.getElementById('btn-csv');";
    html += "  var vrf = document.getElementById('btn-verify');";
    html += "  if (rep) rep.href = '/report?range=' + range;";
    html += "  if (csv) { csv.href = '/export_csv?range=' + range; csv.setAttribute('download', 'cold_chain_' + range + '.csv'); }";
    html += "  if (vrf) vrf.href = '/verify?range=' + range;";
    html += "}";
    html += "</script>";
    html += "</head><body>";

    html += "<div id=\"status-box\">✅ Settings Saved Successfully! Device is restarting...</div>";

    html += "<div id=\"scan-modal\">";
    html += "<div class=\"spinner\"></div>";
    html += "<div style=\"font-size:18px;font-weight:bold;\">🔍 Scanning Thermometers...</div>";
    html += "<div style=\"font-size:13px;margin-top:8px;color:#ddd;\">Please wait (5 seconds)...</div>";
    html += "</div>";

    html += "<div class=\"card\" style=\"background:#eef7ff;border:1.5px solid #1a73e8;\">";
    html += "<h2 style=\"color:#1a73e8;border-color:#d2e3fc;\">📊 Cold Chain Analytics & Tamper-Proof Audit Reports</h2>";
    html += "<p style=\"font-size:13px;color:#444;margin-bottom:12px;\">Export official temperature logs, excursion history, and print-ready PDF audit reports tailored by time window and sampling frequency.</p>";
    html += "<label style=\"font-size:13px;font-weight:bold;color:#1a73e8;margin-bottom:6px;display:block;\">Select Audit Window & Measurement Interval:</label>";
    html += "<select id=\"report_range_sel\" onchange=\"updateReportUrls(this.value)\" style=\"padding:10px;border:1.5px solid #1a73e8;border-radius:6px;font-weight:600;font-size:14px;background:#fff;margin-bottom:15px;cursor:pointer;\">";
    html += "  <option value=\"6h\">6 Hours (15-min intervals)</option>";
    html += "  <option value=\"12h\">12 Hours (15-min intervals)</option>";
    html += "  <option value=\"24h\" selected>24 Hours (30-min intervals)</option>";
    html += "  <option value=\"36h\">36 Hours (30-min intervals)</option>";
    html += "  <option value=\"1w\">Son 1 Hafta / 7 Days (1-hour intervals)</option>";
    html += "  <option value=\"2w\">Son 2 Hafta / 14 Days (2-hour intervals)</option>";
    html += "  <option value=\"4w\">Son 4 Hafta / 28 Days (4-hour intervals)</option>";
    html += "</select>";
    html += "<div style=\"display:flex;gap:10px;flex-wrap:wrap;\">";
    html += "  <a id=\"btn-report\" href=\"/report?range=24h\" target=\"_blank\" style=\"flex:1;min-width:130px;background:#1a73e8;color:#fff;text-decoration:none;padding:12px;border-radius:6px;font-weight:bold;text-align:center;font-size:14px;\">📄 PDF Audit Report</a>";
    html += "  <a id=\"btn-csv\" href=\"/export_csv?range=24h\" download=\"cold_chain_24h.csv\" style=\"flex:1;min-width:130px;background:#34a853;color:#fff;text-decoration:none;padding:12px;border-radius:6px;font-weight:bold;text-align:center;font-size:14px;\">📥 Download CSV</a>";
    html += "  <a id=\"btn-verify\" href=\"/verify?range=24h\" style=\"flex:1;min-width:130px;background:#137333;color:#fff;text-decoration:none;padding:12px;border-radius:6px;font-weight:bold;text-align:center;font-size:14px;\">🛡️ Verify Certificate</a>";
    html += "  <a href=\"/about\" style=\"flex:1;min-width:130px;background:#5f6368;color:#fff;text-decoration:none;padding:12px;border-radius:6px;font-weight:bold;text-align:center;font-size:14px;\">ℹ️ About & Docs</a>";
    html += "</div>";
    html += "</div>";

    html += "<form id=\"cfg-form\" onsubmit=\"submitForm(event)\">";
    html += "<div class=\"card\">";
    html += "<h2>📶 1. Wi-Fi Configuration (Primary & Backup)</h2>";
    html += "<label>Primary Wi-Fi SSID:</label>";
    html += "<input type=\"text\" name=\"ssid\" value=\"" + cfgMgr.config.wifiSsid + "\" required>";
    html += "<label>Primary Wi-Fi Password:</label>";
    html += "<input type=\"password\" name=\"pass\" value=\"" + cfgMgr.config.wifiPass + "\">";

    html += "<label style=\"margin-top:15px;color:#e67e22;\">Backup Wi-Fi SSID [Optional]:</label>";
    html += "<input type=\"text\" name=\"b_ssid\" value=\"" + cfgMgr.config.backupWifiSsid + "\">";
    html += "<label style=\"color:#e67e22;\">Backup Wi-Fi Password:</label>";
    html += "<input type=\"password\" name=\"b_pass\" value=\"" + cfgMgr.config.backupWifiPass + "\">";
    html += "</div>";

    html += "<div class=\"card\">";
    html += "<h2>🔵 2. BLE Thermometer Pairing</h2>";
    if (cfgMgr.config.bleTargetMac.length() > 0) {
      html += "<div style=\"margin-bottom:10px;\"><strong>Paired Device:</strong> " + cfgMgr.config.bleTargetName + " (" + cfgMgr.config.bleTargetMac + ")</div>";
      html += "<a href=\"/clear_ble\" style=\"color:#d93025;font-weight:600;text-decoration:none;\">🗑️ Unpair Device / Auto-Discover</a><br>";
    } else {
      html += "<div style=\"color:#f2994a;font-weight:600;margin-bottom:10px;\">⚠️ No paired device. Closest thermometer will be auto-selected.</div>";
    }

    html += "<a href=\"javascript:void(0);\" onclick=\"triggerScan()\" class=\"btn-scan\">🔄 Scan Nearby Thermometers (5s)</a>";

    html += "<label style=\"margin-top:15px;\">Discovered Thermometers (" + String(discoveredBLEs.size()) + " found):</label>";
    for (size_t i = 0; i < discoveredBLEs.size(); i++) {
      String checked = (discoveredBLEs[i].mac.equalsIgnoreCase(cfgMgr.config.bleTargetMac)) ? "checked" : "";
      html += "<div class=\"device-item\">";
      html += "<div><input type=\"radio\" name=\"sel_ble\" value=\"" + discoveredBLEs[i].mac + "|" + discoveredBLEs[i].name + "\" " + checked + " id=\"ble_" + String(i) + "\"> ";
      html += "<label style=\"display:inline;\" for=\"ble_" + String(i) + "\"><strong>" + discoveredBLEs[i].name + "</strong> (" + discoveredBLEs[i].mac + ")</label></div>";
      html += "<div><span class=\"badge\">" + String(discoveredBLEs[i].rssi) + " dBm</span> <span class=\"badge\">" + String(discoveredBLEs[i].temp, 1) + " &deg;C</span></div>";
      html += "</div>";
    }
    html += "</div>";

    html += "<div class=\"card\">";
    html += "<h2>⏱️ 3. Timing & Scan Intervals</h2>";
    html += "<div class=\"row\"><div class=\"col\">";
    html += "<label>BLE Read Interval (seconds):</label>";
    html += "<input type=\"number\" min=\"5\" name=\"ble_int\" value=\"" + String(cfgMgr.config.bleReadIntervalSec) + "\">";
    html += "<small style=\"color:#666;\">Interval between thermometer reads/logging (e.g. 60s)</small>";
    html += "</div><div class=\"col\">";
    html += "<label>BLE Search Timeout (seconds):</label>";
    html += "<input type=\"number\" min=\"10\" name=\"stg_tout\" value=\"" + String(cfgMgr.config.stageTimeoutSec) + "\">";
    html += "<small style=\"color:#666;\">Search timeout before auto-discovery/fallback (default: 185s)</small>";
    html += "</div></div>";
    html += "</div>";

    html += "<div class=\"card\">";
    html += "<h2>🌡️ 4. Temperature & Limits Settings</h2>";
    html += "<div class=\"row\"><div class=\"col\">";
    html += "<label>Min Limit (&deg;C):</label>";
    html += "<input type=\"number\" step=\"0.1\" name=\"t_min\" value=\"" + String(cfgMgr.config.normalTempMin, 1) + "\">";
    html += "</div><div class=\"col\">";
    html += "<label>Max Limit (&deg;C):</label>";
    html += "<input type=\"number\" step=\"0.1\" name=\"t_max\" value=\"" + String(cfgMgr.config.normalTempMax, 1) + "\">";
    html += "</div></div>";

    html += "<div class=\"row\"><div class=\"col\">";
    html += "<label>Limit Alert Interval (min):</label>";
    html += "<input type=\"number\" name=\"lim_int\" value=\"" + String(cfgMgr.config.limitAlertIntervalMin) + "\">";
    html += "</div></div>";
    html += "</div>";

    html += "<div class=\"card\">";
    html += "<h2>⚡ 5. Power Outage Detection</h2>";
    html += "<label>Power Detection GPIO Pin:</label>";
    html += "<input type=\"number\" name=\"pwr_pin\" value=\"" + String(cfgMgr.config.powerDetectPin) + "\">";

    html += "<div class=\"row\"><div class=\"col\">";
    html += "<label>Power Loss Min Limit (&deg;C):</label>";
    html += "<input type=\"number\" step=\"0.1\" name=\"pwr_t_min\" value=\"" + String(cfgMgr.config.powerLossTempMin, 1) + "\">";
    html += "</div><div class=\"col\">";
    html += "<label>Power Loss Max Limit (&deg;C):</label>";
    html += "<input type=\"number\" step=\"0.1\" name=\"pwr_t_max\" value=\"" + String(cfgMgr.config.powerLossTempMax, 1) + "\">";
    html += "</div></div>";

    html += "<label>Outage Alert Interval (min):</label>";
    html += "<input type=\"number\" name=\"pwr_int\" value=\"" + String(cfgMgr.config.powerLossAlertIntervalMin) + "\">";

    html += "<label>Notification Channels on Outage:</label>";
    html += "<input type=\"checkbox\" name=\"pwr_tg\" value=\"1\" " + String(cfgMgr.config.notifyTelegramOnPowerLoss ? "checked" : "") + " id=\"cb_tg\"> <label style=\"display:inline;\" for=\"cb_tg\">Telegram</label><br>";
    html += "<input type=\"checkbox\" name=\"pwr_wh\" value=\"1\" " + String(cfgMgr.config.notifyWebhookOnPowerLoss ? "checked" : "") + " id=\"cb_wh\"> <label style=\"display:inline;\" for=\"cb_wh\">Webhook</label><br>";
    html += "<input type=\"checkbox\" name=\"pwr_gs\" value=\"1\" " + String(cfgMgr.config.notifySheetsOnPowerLoss ? "checked" : "") + " id=\"cb_gs\"> <label style=\"display:inline;\" for=\"cb_gs\">Google Sheets</label>";
    html += "</div>";

    html += "<div class=\"card\">";
    html += "<h2>🔔 6. Notification Channels & Endpoints</h2>";
    html += "<label>Google Sheets Web App URL:</label>";
    html += "<input type=\"text\" name=\"gs_url\" value=\"" + cfgMgr.config.googleScriptUrl + "\">";
    html += "<label>Custom Webhook URL:</label>";
    html += "<input type=\"text\" name=\"wh_url\" value=\"" + cfgMgr.config.webhookUrl + "\">";
    html += "<label>Telegram Bot Token:</label>";
    html += "<input type=\"text\" name=\"tg_token\" value=\"" + cfgMgr.config.telegramBotToken + "\">";
    html += "<label>Telegram Chat ID:</label>";
    html += "<input type=\"text\" name=\"tg_chat\" value=\"" + cfgMgr.config.telegramChatId + "\">";
    html += "</div>";
    html += "<div class=\"card\">";
    html += "<h2>🎯 7. 4-Point Temperature Calibration & Security Lock</h2>";
    html += "<p style=\"font-size:13px;color:#5f6368;\">Map your sensor readings against a certified master reference thermometer at 4 critical cold-chain points (2°C, 4°C, 6°C, 8°C). The system applies piecewise linear interpolation and calculates the standard deviation (&sigma;).</p>";
    
    html += "<div style=\"background:#f1f3f4;padding:10px;border-radius:6px;margin-bottom:15px;font-size:13px;\">";
    html += "  <strong>Active Profile Date:</strong> " + cfgMgr.config.calDate + "<br>";
    html += "  <strong>Current Standard Deviation (&sigma;):</strong> &plusmn;" + String(cfgMgr.config.calStdDev, 2) + " &deg;C";
    html += "</div>";

    html += "<div style=\"display:grid;grid-template-columns:repeat(2,1fr);gap:10px;\">";
    html += "  <div><label>Ref 2.0 &deg;C &rarr; Sensor Reads:</label><input type=\"number\" step=\"0.01\" name=\"cal_r2\" value=\"" + String(cfgMgr.config.calRaw2, 2) + "\"></div>";
    html += "  <div><label>Ref 4.0 &deg;C &rarr; Sensor Reads:</label><input type=\"number\" step=\"0.01\" name=\"cal_r4\" value=\"" + String(cfgMgr.config.calRaw4, 2) + "\"></div>";
    html += "  <div><label>Ref 6.0 &deg;C &rarr; Sensor Reads:</label><input type=\"number\" step=\"0.01\" name=\"cal_r6\" value=\"" + String(cfgMgr.config.calRaw6, 2) + "\"></div>";
    html += "  <div><label>Ref 8.0 &deg;C &rarr; Sensor Reads:</label><input type=\"number\" step=\"0.01\" name=\"cal_r8\" value=\"" + String(cfgMgr.config.calRaw8, 2) + "\"></div>";
    html += "</div>";

    html += "<hr style=\"border:none;border-top:1px solid #eee;margin:15px 0;\">";
    if (cfgMgr.config.calPassword.length() > 0) {
      html += "<div style=\"color:#d93025;font-weight:bold;margin-bottom:8px;\">🔒 Password Protected: Authorization Required</div>";
      html += "<label>Current Calibration Password:</label>";
      html += "<input type=\"password\" name=\"cal_pass_auth\" placeholder=\"Enter password to apply calibration changes\">";
      html += "<label>Change Password (leave blank to keep current):</label>";
      html += "<input type=\"password\" name=\"cal_pass_new\" placeholder=\"New password (optional)\">";
      html += "<input type=\"checkbox\" name=\"clear_cal_pass\" value=\"1\" id=\"cb_clear_cal\"><label style=\"display:inline;\" for=\"cb_clear_cal\"> Remove password protection (Unlock)</label>";
    } else {
      html += "<div style=\"color:#137333;font-weight:bold;margin-bottom:8px;\">🔓 Unlocked (No Password Set)</div>";
      html += "<label>Set Optional Password to Lock Calibration:</label>";
      html += "<input type=\"password\" name=\"cal_pass_new\" placeholder=\"Set password (leave blank to remain unlocked)\">";
      html += "<small style=\"color:#5f6368;\">⚠️ Notice: If a password is set and forgotten, the firmware must be reflashed via USB to clear it.</small>";
    }
    html += "</div>";

    html += "<button type=\"submit\" id=\"save-btn\" class=\"btn\">💾 SAVE ALL & RESTART</button>";
    html += "</form>";

    html += "<div class=\"card\" style=\"border:1px solid #fce8e6;margin-top:20px;\">";
    html += "<h2 style=\"color:#d93025;border-color:#fce8e6;\">⚠️ Factory Reset (Format NVS)</h2>";
    html += "<p style=\"font-size:13px;color:#666;\">Clears all stored Wi-Fi credentials, paired thermometers, and notification configurations.</p>";
    html += "<a href=\"/factory_reset\" onclick=\"return confirm('All settings will be erased and reset to defaults. Continue?');\" class=\"btn-danger\">🚨 FACTORY RESET (CLEAR NVS)</a>";
    html += "</div>";

    html += "</body></html>";
    return html;
  }

public:
  WebPortal() : server(80) {}

  void start() {
    Serial.println("[PORTAL] Initializing SoftAP mode...");
    WiFi.disconnect(true);
    delay(100);
    WiFi.mode(WIFI_AP);
    delay(50);
    WiFi.setSleep(false); // Keep Wi-Fi radio fully active for reliable phone association

    IPAddress local_ip(192, 168, 4, 1);
    IPAddress gateway(192, 168, 4, 1);
    IPAddress subnet(255, 255, 255, 0);
    WiFi.softAPConfig(local_ip, gateway, subnet);

    bool apOk = WiFi.softAP("Thermo_Obs", cfgMgr.config.apPassword.c_str(), 1, 0, 4);
    if (apOk) {
      Serial.printf("[PORTAL] AP Started: Thermo_Obs | Password: %s | IP: 192.168.4.1\n", cfgMgr.config.apPassword.c_str());
    } else {
      Serial.println("[PORTAL] ❌ WiFi.softAP() FAILED to start!");
    }

    dnsServer.start(53, "*", local_ip);

    // Diagnostics for phone connection tracking
    WiFi.onEvent([](WiFiEvent_t event, WiFiEventInfo_t info) {
      if (event == ARDUINO_EVENT_WIFI_AP_START) {
        Serial.println("[AP] SoftAP Started.");
      } else if (event == ARDUINO_EVENT_WIFI_AP_STACONNECTED) {
        Serial.printf("[AP] 📱 Phone Associated! MAC: %02X:%02X:%02X:%02X:%02X:%02X\n",
                      info.wifi_ap_staconnected.mac[0], info.wifi_ap_staconnected.mac[1],
                      info.wifi_ap_staconnected.mac[2], info.wifi_ap_staconnected.mac[3],
                      info.wifi_ap_staconnected.mac[4], info.wifi_ap_staconnected.mac[5]);
      } else if (event == ARDUINO_EVENT_WIFI_AP_STAIPASSIGNED) {
        Serial.printf("[AP] 🌐 Phone assigned IP: %s\n", IPAddress(info.wifi_ap_staipassigned.ip.addr).toString().c_str());
      } else if (event == ARDUINO_EVENT_WIFI_AP_STADISCONNECTED) {
        Serial.println("[AP] 📴 Phone Disconnected.");
      }
    });

    Serial.printf("[PORTAL] Free Heap: %d bytes (Min Ever: %d bytes)\n", ESP.getFreeHeap(), ESP.getMinFreeHeap());

    // Captive Portal Detection Endpoints (Android, iOS, Windows)
    server.on("/generate_204", HTTP_ANY, [this]() {
      Serial.println("[PORTAL] Captive probe /generate_204 (Android)");
      server.sendHeader("Location", "http://192.168.4.1/");
      server.send(302, "text/plain", "");
    });
    server.on("/gen_204", HTTP_ANY, [this]() {
      server.sendHeader("Location", "http://192.168.4.1/");
      server.send(302, "text/plain", "");
    });
    server.on("/hotspot-detect.html", HTTP_ANY, [this]() {
      Serial.println("[PORTAL] Captive probe /hotspot-detect.html (Apple iOS)");
      server.sendHeader("Location", "http://192.168.4.1/");
      server.send(302, "text/plain", "");
    });
    server.on("/canonical.html", HTTP_ANY, [this]() {
      server.sendHeader("Location", "http://192.168.4.1/");
      server.send(302, "text/plain", "");
    });
    server.on("/connecttest.txt", HTTP_ANY, [this]() {
      server.sendHeader("Location", "http://192.168.4.1/");
      server.send(302, "text/plain", "");
    });
    server.on("/ncsi.txt", HTTP_ANY, [this]() {
      server.sendHeader("Location", "http://192.168.4.1/");
      server.send(302, "text/plain", "");
    });

    server.on("/favicon.ico", HTTP_GET, [this]() {
      server.send(204);
    });

    server.on("/", HTTP_ANY, [this]() {
      Serial.printf("[PORTAL] 📄 Serving / (index page) to client. Free Heap: %d bytes\n", ESP.getFreeHeap());
      server.sendHeader("Connection", "close");
      server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
      server.send(200, "text/html; charset=utf-8", buildHtml());
      Serial.println("[PORTAL] ✅ / (index page) served successfully.");
    });

    server.on("/report", HTTP_GET, [this]() {
      String range = server.hasArg("range") ? server.arg("range") : "24h";
      Serial.printf("[PORTAL] Serving /report (range=%s)...\n", range.c_str());
      server.sendHeader("Connection", "close");
      server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
      server.send(200, "text/html; charset=utf-8", ReportGenerator::buildPdfReportHtml(range));
    });

    server.on("/about", HTTP_GET, [this]() {
      server.sendHeader("Connection", "close");
      server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
      server.send(200, "text/html; charset=utf-8", AboutPageGenerator::buildAboutHtml());
    });

    server.on("/verify", HTTP_GET, [this]() {
      String qCert = server.hasArg("cert") ? server.arg("cert") : "";
      String qHash = server.hasArg("hash") ? server.arg("hash") : "";
      String qRange = server.hasArg("range") ? server.arg("range") : "24h";
      server.sendHeader("Connection", "close");
      server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
      server.send(200, "text/html; charset=utf-8", ReportGenerator::buildVerificationHtml(qCert, qHash, qRange));
    });

    server.on("/export_csv", HTTP_GET, [this]() {
      String range = server.hasArg("range") ? server.arg("range") : "24h";
      const RangeConfig* cfg = ReportGenerator::getRangeConfig(range);
      server.sendHeader("Content-Disposition", "attachment; filename=\"cold_chain_" + String(cfg->key) + ".csv\"");
      server.setContentLength(CONTENT_LENGTH_UNKNOWN);

      uint32_t nowEpoch = (uint32_t)time(nullptr);
      bool hasRealTime = (nowEpoch > 1000000000UL);
      uint32_t cutoffTs = hasRealTime ? (nowEpoch - cfg->durationSec) : 0;

      int startOffset = 0;
      if (!hasRealTime && historyCount > cfg->maxSamples) {
        startOffset = historyCount - cfg->maxSamples;
      }

      int filteredCount = 0;
      for (int i = startOffset; i < historyCount; i++) {
        int idx = (historyHead - historyCount + i + HISTORY_SIZE) % HISTORY_SIZE;
        if (tempHistory[idx].temp <= -9990) continue;
        if (hasRealTime && tempHistory[idx].timestamp < cutoffTs) continue;
        filteredCount++;
      }

      String csvMeta = "MAC:" + ReportGenerator::getDeviceHardwareMac() + "|CAL:" + cfgMgr.config.calDate + "|COUNT:" + String(filteredCount) + "|RANGE:" + String(cfg->key);
      String csvHash = ReportGenerator::computeIntegrityHash(csvMeta);
      String certId = ReportGenerator::getCertificateId(csvHash);

      String header = "# Thermo_Obs Cold Chain Audit Data (" + String(cfg->label) + " Tamper-Proof Export)\r\n";
      header += "# Certificate ID: " + certId + "\r\n";
      header += "# Hardware Identity (MAC): " + ReportGenerator::getDeviceHardwareMac() + "\r\n";
      header += "# Cryptographic SHA-256 Digest: " + csvHash + "\r\n";
      header += "# Audit Window: " + String(cfg->label) + " (" + String(cfg->displayFreqText) + " Display Intervals)\r\n";
      header += "# Calibration Date: " + cfgMgr.config.calDate + "\r\n";
      header += "# Calibration Points: Ref[2.0, 4.0, 6.0, 8.0] -> Sensor[" +
                String(cfgMgr.config.calRaw2, 2) + ", " +
                String(cfgMgr.config.calRaw4, 2) + ", " +
                String(cfgMgr.config.calRaw6, 2) + ", " +
                String(cfgMgr.config.calRaw8, 2) + "]\r\n";
      header += "# Calibration Standard Deviation: " + String(cfgMgr.config.calStdDev, 2) + " C\r\n";
      header += "Timestamp,DateTime,Temperature_C,Status\r\n";
      server.send(200, "text/csv", header);

      for (int i = startOffset; i < historyCount; i++) {
        int idx = (historyHead - historyCount + i + HISTORY_SIZE) % HISTORY_SIZE;
        if (tempHistory[idx].temp <= -9990) continue;
        if (hasRealTime && tempHistory[idx].timestamp < cutoffTs) continue;

        float tVal = tempHistory[idx].temp / 10.0f;
        String line = String(tempHistory[idx].timestamp) + "," +
                      ReportGenerator::formatTimestamp(tempHistory[idx].timestamp) + "," +
                      String(tVal, 1) + ",";
        if (tVal <= 2.0f) line += "FREEZE_VIOLATION\r\n";
        else if (tVal >= 8.0f) line += "HEAT_VIOLATION\r\n";
        else line += "NORMAL\r\n";

        server.sendContent(line);
      }
      server.sendContent("");
    });

    server.on("/clear_ble", HTTP_GET, [this]() {
      cfgMgr.clearBLE();
      server.sendHeader("Location", "/");
      server.send(302, "text/plain", "");
    });

    server.on("/trigger_scan", HTTP_GET, [this]() {
      Serial.println("[PORTAL] On-demand BLE scan started (5s)...");
      discoveredBLEs.clear();
      if (pBLEScan) {
        pBLEScan->start(5, false);
        pBLEScan->clearResults();
      }
      server.sendHeader("Connection", "close");
      server.send(200, "text/plain", "OK");
    });

    server.on("/factory_reset", HTTP_GET, [this]() {
      cfgMgr.factoryReset();
      String resp = "<html><body style=\"font-family:sans-serif;text-align:center;padding:50px;\">";
      resp += "<h2 style=\"color:#d93025;\">🚨 All NVS Settings Erased!</h2><p>Device restarting...</p></body></html>";
      server.sendHeader("Connection", "close");
      server.send(200, "text/html; charset=utf-8", resp);
      delay(1500);
      ESP.restart();
    });

    server.on("/save", HTTP_POST, [this]() {
      if (server.hasArg("ssid")) cfgMgr.config.wifiSsid = server.arg("ssid");
      if (server.hasArg("pass")) cfgMgr.config.wifiPass = server.arg("pass");
      if (server.hasArg("b_ssid")) cfgMgr.config.backupWifiSsid = server.arg("b_ssid");
      if (server.hasArg("b_pass")) cfgMgr.config.backupWifiPass = server.arg("b_pass");

      if (server.hasArg("sel_ble")) {
        String sel = server.arg("sel_ble");
        int sep = sel.indexOf('|');
        if (sep != -1) {
          cfgMgr.config.bleTargetMac = sel.substring(0, sep);
          cfgMgr.config.bleTargetName = sel.substring(sep + 1);
        }
      }

      if (server.hasArg("ble_int")) cfgMgr.config.bleReadIntervalSec = server.arg("ble_int").toInt();
      if (server.hasArg("stg_tout")) cfgMgr.config.stageTimeoutSec = server.arg("stg_tout").toInt();

      if (server.hasArg("t_min")) cfgMgr.config.normalTempMin = server.arg("t_min").toFloat();
      if (server.hasArg("t_max")) cfgMgr.config.normalTempMax = server.arg("t_max").toFloat();
      if (server.hasArg("lim_int")) cfgMgr.config.limitAlertIntervalMin = server.arg("lim_int").toInt();

      if (server.hasArg("pwr_pin")) cfgMgr.config.powerDetectPin = server.arg("pwr_pin").toInt();
      if (server.hasArg("pwr_t_min")) cfgMgr.config.powerLossTempMin = server.arg("pwr_t_min").toFloat();
      if (server.hasArg("pwr_t_max")) cfgMgr.config.powerLossTempMax = server.arg("pwr_t_max").toFloat();
      if (server.hasArg("pwr_int")) cfgMgr.config.powerLossAlertIntervalMin = server.arg("pwr_int").toInt();

      cfgMgr.config.notifyTelegramOnPowerLoss = server.hasArg("pwr_tg");
      cfgMgr.config.notifyWebhookOnPowerLoss = server.hasArg("pwr_wh");
      cfgMgr.config.notifySheetsOnPowerLoss = server.hasArg("pwr_gs");

      if (server.hasArg("gs_url")) cfgMgr.config.googleScriptUrl = server.arg("gs_url");
      if (server.hasArg("wh_url")) cfgMgr.config.webhookUrl = server.arg("wh_url");
      if (server.hasArg("tg_token")) cfgMgr.config.telegramBotToken = server.arg("tg_token");
      if (server.hasArg("tg_chat")) cfgMgr.config.telegramChatId = server.arg("tg_chat");

      // Calibration Authorization & Update
      bool canUpdateCal = false;
      if (cfgMgr.config.calPassword.length() == 0) {
        canUpdateCal = true;
      } else {
        if (server.hasArg("cal_pass_auth") && server.arg("cal_pass_auth") == cfgMgr.config.calPassword) {
          canUpdateCal = true;
        } else {
          Serial.println("[PORTAL] Calibration save rejected: Incorrect authorization password!");
        }
      }

      if (canUpdateCal) {
        bool calChanged = false;
        if (server.hasArg("cal_r2")) {
          float v = server.arg("cal_r2").toFloat();
          if (abs(v - cfgMgr.config.calRaw2) > 0.001f) { cfgMgr.config.calRaw2 = v; calChanged = true; }
        }
        if (server.hasArg("cal_r4")) {
          float v = server.arg("cal_r4").toFloat();
          if (abs(v - cfgMgr.config.calRaw4) > 0.001f) { cfgMgr.config.calRaw4 = v; calChanged = true; }
        }
        if (server.hasArg("cal_r6")) {
          float v = server.arg("cal_r6").toFloat();
          if (abs(v - cfgMgr.config.calRaw6) > 0.001f) { cfgMgr.config.calRaw6 = v; calChanged = true; }
        }
        if (server.hasArg("cal_r8")) {
          float v = server.arg("cal_r8").toFloat();
          if (abs(v - cfgMgr.config.calRaw8) > 0.001f) { cfgMgr.config.calRaw8 = v; calChanged = true; }
        }

        if (server.hasArg("clear_cal_pass")) {
          cfgMgr.config.calPassword = "";
        } else if (server.hasArg("cal_pass_new")) {
          String newPass = server.arg("cal_pass_new");
          newPass.trim();
          if (newPass.length() > 0) {
            cfgMgr.config.calPassword = newPass;
          }
        }

        if (calChanged) {
          time_t nowSec = time(nullptr);
          if (nowSec > 1000000000) {
            struct tm* t = localtime(&nowSec);
            char buf[32];
            snprintf(buf, sizeof(buf), "%02d.%02d.%04d %02d:%02d",
                     t->tm_mday, t->tm_mon + 1, t->tm_year + 1900,
                     t->tm_hour, t->tm_min);
            cfgMgr.config.calDate = String(buf);
          } else {
            cfgMgr.config.calDate = "Updated Profile";
          }
        }
      }

      cfgMgr.save();

      server.sendHeader("Connection", "close");
      server.send(200, "text/plain; charset=utf-8", "OK");
      delay(1500);
      ESP.restart();
    });

    server.onNotFound([this]() {
      String uri = server.uri();
      String host = server.hostHeader();
      Serial.printf("[PORTAL] 🔀 onNotFound: Host: %s | URI: %s -> Redirecting to /\n", host.c_str(), uri.c_str());
      server.sendHeader("Location", "http://192.168.4.1/");
      server.send(302, "text/plain", "");
    });

    server.begin();
    isRunning = true;
    Serial.printf("[PORTAL] AP Started: Thermo_Obs | Password: %s | IP: 192.168.4.1\n", cfgMgr.config.apPassword.c_str());
  }

  void handle() {
    if (isRunning) {
      dnsServer.processNextRequest();
      server.handleClient();
    }
  }
};