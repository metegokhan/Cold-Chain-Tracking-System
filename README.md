# Thermo_Obs: ESP32-C3 Smart BLE Thermometer Observer & Cloud Logger

**Thermo_Obs** is an open-source, resilient IoT monitoring system designed for continuous temperature/humidity observation, cloud logging, power outage alerting, and visual OLED analytics. It runs on the ultra-compact **ESP32-C3 0.42" OLED development board** (with built-in display), tracking BLE advertisements from Xiaomi Mijia thermometers flashed with custom ATC / BTHome firmware.

---

## 🌟 Key Features

- **Xiaomi LYWSD03MMC / BTHome V2 BLE Scanner:** Captures temperature, humidity, battery percentage, voltage, and RSSI with zero pairing PIN required.
- **Embedded Web Configuration Portal (WiFiManager):**
  - Standalone SoftAP (`Thermo_Obs`) with persistent randomly-generated 8-digit password.
  - On-demand BLE scanning (5 seconds) with interactive device selection.
  - Primary and backup Wi-Fi configuration with automatic failover.
  - **Customizable BLE Reading Interval:** Define how often data is read and reported (e.g. every 60s) with non-blocking `millis()` timing.
  - **Customizable BLE Search Timeout:** Configurable timeout period (default: 185s) before auto-discovery or fallback.
  - NVS persistent storage & One-click Factory Reset.
- **Dual-Mode Coexistence & Timeout State Machine:**
  - Dedicated configurable BLE acquisition window and non-blocking Wi-Fi upload stage.
  - Never submits stale or frozen measurements.
- **Physical BOOT Button UI Navigation:**
  - **Short Press:** Browse through 8 informative real-time OLED screens without background interference.
  - **5-Second Long Press (with on-screen progress bar):** Opens the Configuration Menu (`WPS`, `Web Portal`, `BLE Discover`).
  - 10-second inactivity timeout returning to the primary screen.
- **36-Hour Temperature Analytics:**
  - Real-time rolling memory buffer (2160 samples).
  - Displays 36-hour Minimum and Maximum temperatures along with the total duration (in minutes) the environment stayed within $\pm 0.5^\circ\text{C}$ of those extremes.
- **Multi-Channel Alert System:**
  - **Google Sheets Integration:** Continuous logging via Google Apps Script.
  - **Telegram Bot API:** Instant threshold alarm and power outage notifications.
  - **Custom Webhook:** REST endpoint for Home Assistant, Node-RED, Discord, or custom servers.
- **Mains Power Outage Detection:**
  - Monitors mains presence via dedicated GPIO with configurable emergency temperature thresholds.

---

## 🛠️ Hardware Requirements

1. **Development Board:** ESP32-C3 0.42" OLED Board (all-in-one board with on-board 0.42" SSD1306 I2C OLED display).
2. **Thermometer:** Xiaomi Mijia Bluetooth Thermometer 2 (**LYWSD03MMC**) or compatible ATC/BTHome BLE beacon.
3. **Power Detection (Optional):** Optocoupler or voltage divider connected to `GPIO 4`.

---

## 📡 Xiaomi Thermometer Custom Firmware (BTHome / ATC)

This project is tailored to decode **BTHome V2** and **ATC custom firmware** format (`Service UUID: 0xFCD2`).

- **Web Flasher Tool:** [Telink Flasher by pvvx](https://pvvx.github.io/ATC_MiThermometer/TelinkMiFlasher.html)
- **Source Repository:** [pvvx/ATC_MiThermometer (GitHub)](https://github.com/pvvx/ATC_MiThermometer)
- **Recommended Settings in Flasher:**
  - **Advertising Type:** `BTHome V2` or `Custom / ATC`
  - **Advertising Interval:** `2500 ms` to `5000 ms` (for optimal battery life and fast ESP32 discovery)
  - **RF TX Power:** `0 dBm` or `+3 dBm`

---

## 📊 Google Sheets & Apps Script Setup

1. Create a new [Google Sheets](https://sheets.new) document.
2. Navigate to **Extensions (Uzantılar) -> Apps Script**.
3. Replace the code in the editor with the following Google Apps Script:

```javascript
/**
 * Thermo_Obs Google Sheets Receiver & Alert Macro
 */
function doGet(e) {
  var sheet = SpreadsheetApp.getActiveSpreadsheet().getActiveSheet();
  var params = e.parameter;

  var date = Utilities.formatDate(new Date(), "GMT+3", "dd.MM.yyyy HH:mm:ss");
  var device = params.device || "ATC_UNKNOWN";
  var temp = (params.temp === "-" || isNaN(parseFloat(params.temp))) ? "-" : parseFloat(params.temp);
  var hum = (params.hum === "-" || isNaN(parseFloat(params.hum))) ? "-" : parseFloat(params.hum);
  var bat = (params.bat === "-" || isNaN(parseInt(params.bat))) ? "-" : parseInt(params.bat);
  var volt = (params.volt === "-" || isNaN(parseFloat(params.volt))) ? "-" : parseFloat(params.volt);
  var rssi = params.rssi || "-";
  var pwr = params.pwr || "ONLINE";
  var note = params.note || "Normal";

  // Append new telemetry row
  sheet.appendRow([date, device, temp, hum, bat, volt, rssi, pwr, note]);

  // Optional: Trigger Temperature Limit Alert Email
  if (temp !== "-" && (temp < 2.0 || temp > 8.0)) {
    var recipient = Session.getActiveUser().getEmail();
    MailApp.sendEmail(recipient, "🚨 Thermo_Obs Alert: " + device, 
                      "Temperature threshold exceeded!\nCurrent Temperature: " + temp + " °C\nPower: " + pwr);
  }

  return ContentService.createTextOutput("OK").setMimeType(ContentService.MimeType.TEXT);
}
```

4. Click **Deploy (Dağıt) -> New Deployment (Yeni Dağıtım)**.
5. Select type **Web App (Web Uygulaması)**:
   - **Execute as:** `Me (Ben)`
   - **Who has access:** `Anyone (Herkes)`
6. Copy the **Web App URL** and paste it into the Thermo_Obs Web Portal!

---

## 🖥️ OLED Screen Map & Button Controls

| Screen | View | Description |
| :--- | :--- | :--- |
| **1. Main Temp** | `[ATC_XXXX  w:V b:V] 24.5°` | Large real-time temperature with Wi-Fi (`w`) and BLE (`b`) health flags. |
| **2. Humidity** | `[HUMIDITY] 58% RH` | Relative humidity percentage. |
| **3. Battery** | `[BATTERY] 98% 3.02V` | Coin cell battery percentage and measured voltage. |
| **4. BLE Signal**| `[BLE SIGNAL] -62 dBm`| Received Signal Strength Indicator (RSSI). |
| **5. 36h Min** | `[36h MINIMUM] +2.8° 42 min`| Lowest temperature recorded in 36h and total duration in $\pm 0.5^\circ\text{C}$ band. |
| **6. 36h Max** | `[36h MAXIMUM] +7.9° 18 min`| Highest temperature recorded in 36h and total duration in $\pm 0.5^\circ\text{C}$ band. |
| **7. Wi-Fi Info**| `[WIFI INFO] SSID / State / IP` | Current active Wi-Fi network and assigned IP address. |
| **8. BLE Info** | `[BLE SENSOR INFO] Name / MAC`| Target thermometer Bluetooth MAC address and details. |

---

## ⚙️ Compilation & Flashing

Use **Arduino CLI** or **Arduino IDE (ESP32 Board Package $\ge 3.0.0$ / Arduino ESP32 core)**:

```bash
# Compile using Huge App partition scheme
arduino-cli compile --fqbn esp32:esp32:esp32c3:CDCOnBoot=cdc,PartitionScheme=huge_app .

# Upload to ESP32-C3 port
arduino-cli upload -p COM_PORT --fqbn esp32:esp32:esp32c3:CDCOnBoot=cdc,PartitionScheme=huge_app .
```

---

## 📄 License
This project is released under the **MIT License**.