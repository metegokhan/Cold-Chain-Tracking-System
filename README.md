# Thermo_Obs: ESP32-C3 Smart Cold Chain Monitoring & Logging System

**Thermo_Obs** is an open-source, resilient **IoT Cold Chain Monitoring and Data Logging System** engineered to safeguard vaccines, pharmaceuticals, biological samples, and perishable foodstuffs across storage and transit.

Built around the ultra-compact **ESP32-C3 0.42" OLED development board** (RISC-V 32-bit @ 160MHz), it continuously captures temperature, relative humidity, battery level, and link quality from nearby **Xiaomi Mijia (LYWSD03MMC)** BLE thermometers running custom ATC / BTHome firmware. It maintains a **30-day wear-leveled historical record in onboard LittleFS flash**, synchronizes with **NTP internet time**, logs telemetry to Google Sheets, and serves a regulatory **audit-ready PDF report and raw CSV** directly through its built-in web portal.

---

## ⚠️ Disclaimer of Liability

> **This project is open-source and the workflows, scripts, or examples provided may not be error-free. The user is solely responsible for any errors, data loss, or other adverse consequences that may arise during the use of the software; the developer or project owners assume no liability for such damages.**
>
> *(This system is provided "as is" without warranty of any kind, express or implied. Users are solely responsible for hardware calibration, RF link testing, power fail-safe verification, and ensuring compliance with applicable regulatory cold-chain standards such as WHO PQS, FDA, or local health authority guidelines.)*

---

## 📱 Visual Menu & Screen Hierarchy

The entire device interface is operated using a **single physical button (BOOT button - GPIO 9)**.

### 1. Navigation Flowchart & State Machine

```text
[Normal Operation / Real-Time Telemetry Screens]
   │
   │─── (Hold BOOT button for 5 seconds) ───► [Progress Bar: Opening Menu..]
   │
   ▼
┌────────────────────────────────────────────────────────────────────────┐
│                              CONFIG MENU                               │
├──────────────────────┬──────────────────────────┬──────────────────────┤
│    1. WPS Setup      │      2. Web Portal       │     3. Discover      │
├──────────────────────┼──────────────────────────┼──────────────────────┤
│ Press the WPS button │ Starts a standalone      │ Performs an active   │
│ on your router to    │ Wi-Fi Access Point:      │ scan for nearby BLE  │
│ pair Wi-Fi           │   SSID: Thermo_Obs       │ thermometers; pairs  │
│ automatically without│   IP  : 192.168.4.1      │ with the strongest   │
│ typing a password.   │ Access configuration,    │ advertising beacon.  │
│                      │ PDF reports, & CSV.      │                      │
└──────────────────────┴──────────────────────────┴──────────────────────┘
   │
   └────► (10 seconds of inactivity flushes history to LittleFS and triggers soft reboot)
```

---

### 2. OLED Display Mockups (0.42" SSD1306 - 72x40 Visible Pixel Window)

In normal operation mode, a **short press of the BOOT button** advances to the next information screen. Active background RF scanning is temporarily halted during user navigation to provide instant, flicker-free readability. The interface automatically returns to Screen 1 after 10 seconds of inactivity.

```text
Screen 1: Primary Temperature & Link    Screen 2: Relative Humidity
┌──────────────────────────────┐        ┌──────────────────────────────┐
│  ATC_2A40            w:V b:V │        │  HUMIDITY                    │
│                              │        │                              │
│         4.8°                 │        │            %58 RH            │
│                              │        │                              │
└──────────────────────────────┘        └──────────────────────────────┘
(w: Wi-Fi status, b: Bluetooth)         (Relative humidity percentage)

Screen 3: Sensor Battery & Voltage      Screen 4: Bluetooth Signal (RSSI)
┌──────────────────────────────┐        ┌──────────────────────────────┐
│  BATTERY                     │        │  BLE SIGNAL                  │
│                              │        │                              │
│         %98   3.04V          │        │           -62 dBm            │
│                              │        │                              │
└──────────────────────────────┘        └──────────────────────────────┘
(CR2032 coin cell % and voltage)        (Received signal strength in dBm)

Screen 5: 30-Day Minimum Temp           Screen 6: 30-Day Maximum Temp
┌──────────────────────────────┐        ┌──────────────────────────────┐
│  30d MINIMUM                 │        │  30d MAXIMUM                 │
│  +2.3°                       │        │  +7.6°                       │
│  18h (+-0.5)                 │        │  2.1d (+-0.5)                │
└──────────────────────────────┘        └──────────────────────────────┘
(30-day min temp & hours in band)       (30-day max temp & days in band)

Screen 7: Wi-Fi Network Telemetry       Screen 8: Paired Sensor Identity
┌──────────────────────────────┐        ┌──────────────────────────────┐
│  WIFI INFO                   │        │  BLE SENSOR INFO             │
│  SSID: ColdRoom_2G           │        │  Name: ATC_2A40              │
│  State: Connected            │        │  MAC: A4:C1:38:2A:40:11      │
│  IP: 192.168.1.105           │        │  RSSI: -62 dBm               │
└──────────────────────────────┘        └──────────────────────────────┘
(Active SSID & DHCP IP address)         (Sensor Bluetooth MAC & name)

Interactive Configuration Menu (Hold BOOT for 5 seconds):
┌──────────────────────────────┐
│         CONFIG MENU          │
│  > 1.WPS Setup               │
│    2.Web Portal              │
│    3.Discover                │
└──────────────────────────────┘
(Short press: Change selection | Hold 2s: Launch selected action)
```

---

## 🛠️ Hardware Architecture & Pinout Map

| Component | Model / Description | Pinout / ESP32-C3 GPIO |
| :--- | :--- | :--- |
| **Mainboard** | **ESP32-C3 0.42" OLED Development Board** (RISC-V 160MHz, 4MB Flash) | All-in-one MCU & display board |
| **OLED Display** | 0.42" SSD1306 Monochrome I2C (72x40 active window) | `SCL: GPIO 6` , `SDA: GPIO 5` |
| **Control Button** | On-board `BOOT` push-button | `GPIO 9` (Internal Pull-Up, Active LOW) |
| **Wireless Sensor** | Xiaomi Mijia Bluetooth Thermometer 2 (**LYWSD03MMC**) | BLE 5.0 (BTHome V2 / ATC Beacon) |
| **Power Outage Sensor**| 5V USB sensing circuit / Optocoupler *(Optional)* | `GPIO 4` (NVS-configurable Active Low/High) |

> 📌 **Board Pinout & Specs Reference:** [Codey Online - ESP32-C3 OLED 0.42" Pinout & Specs](https://codey.online/boards/esp32-c3-oled)

---

## 📚 Required Libraries & Versions

The firmware has been compiled and verified with the following components and core libraries:

| Library / Core | Recommended Version | Author / Source | Purpose / Installation |
| :--- | :--- | :--- | :--- |
| **esp32:esp32 (Core)** | **3.0.0 – 3.3.11** | [Espressif Systems](https://github.com/espressif/arduino-esp32) | `arduino-cli core install esp32:esp32` |
| **U8g2** | **2.36.19** (or $\ge 2.35$) | [olikraus (GitHub)](https://github.com/olikraus/u8g2) | `arduino-cli lib install "U8g2"` |
| **LittleFS** | Built-in | Espressif ESP32 Core | Wear-leveled persistent flash file system |
| **WiFi / WebServer / DNSServer** | Built-in | Espressif ESP32 Core | Captive portal & AP networking stack |
| **BLE (Bluetooth Low Energy)** | Built-in | Espressif ESP32 Core | Non-blocking BLE beacon advertisement scanner |
| **Chart.js** | **v4.x (CDN)** | [Chartjs.org](https://www.chartjs.org/) | Browser-rendered interactive time-series charts |

---

## 🚀 Step-by-Step Installation & Quickstart Guide

### Step 1: Flash Xiaomi Mijia Thermometer with Custom Firmware (1 Minute)
1. Open [TelinkMiFlasher by pvvx](https://pvvx.github.io/ATC_MiThermometer/TelinkMiFlasher.html) in a Web Bluetooth compatible browser (Google Chrome).
2. Click **Connect** and select your LYWSD03MMC thermometer.
3. Click **Do Activation**, then click **Custom Firmware (pvvx)** to flash.
4. Set the following recommended parameters:
   - **Advertising Type:** `BTHome V2` or `Custom / ATC`
   - **Advertising Interval:** `2500 ms` to `3500 ms`
5. Click **Send Settings**.

---

### Step 2: Compile & Flash the Firmware to ESP32-C3
Connect your ESP32-C3 board to your PC via a USB-C data cable (e.g., `COM4`).

**Option A: Using Arduino CLI (Command Line):**
```bash
# 1. Install board core and libraries
arduino-cli core install esp32:esp32
arduino-cli lib install "U8g2"

# 2. Compile using Huge App partition scheme
arduino-cli compile --fqbn esp32:esp32:esp32c3:CDCOnBoot=cdc,PartitionScheme=huge_app .

# 3. Flash to ESP32-C3 serial port
arduino-cli upload -p COM4 --fqbn esp32:esp32:esp32c3:CDCOnBoot=cdc,PartitionScheme=huge_app .
```

**Option B: Using Arduino IDE:**
1. Select `Tools -> Board -> ESP32 Arduino -> ESP32C3 Dev Module`.
2. Set `Tools -> USB CDC On Boot` to **Enabled**.
3. Set `Tools -> Partition Scheme` to **Huge APP (3MB No OTA/1MB SPIFFS)**.
4. Select your serial COM port and click **Upload**.

---

### Step 3: Device Initialization & Wi-Fi Configuration
1. Power on the device. **Hold the BOOT button for 5 seconds** until the loading bar fills to open the Config Menu.
2. Short press once to highlight `> 2.Web Portal`, then hold for 1.5 seconds to launch.
3. Connect your phone or laptop to the Wi-Fi network **`Thermo_Obs`** using the 8-digit password shown on the OLED screen.
4. Open **`http://192.168.4.1`** in any web browser:
   - **Wi-Fi Settings:** Enter your primary and optional backup Wi-Fi credentials.
   - **Thermometer Pairing:** Click **🔄 Scan Nearby Thermometers** (5 seconds) and select your target sensor from the discovered list.
   - **Thresholds:** Set standard storage limits (Default: $2.0^\circ\text{C}$ to $8.0^\circ\text{C}$).
   - **Alert Channels:** Enter your Google Sheets Web App URL, Telegram Bot Token, or Custom Webhook URL.
5. Click **💾 SAVE ALL & RESTART**. The device saves all parameters to NVS and begins monitoring.

---

### Step 4: Exporting 30-Day Audit Reports & CSV Data
At any point, access the Web Portal (`192.168.4.1`) or navigate via local network IP to access:
- **📄 View / Print PDF Report (`/report`):** Generates a print-ready A4 compliance audit report complete with a 30-day time-series curve (Chart.js), Minimum/Maximum temperature timestamps, average temperature, and a breach duration table detailing exact exposure times for freeze risk ($\le 2.0^\circ\text{C}$) and warmth excursion ($\ge 8.0^\circ\text{C}$).
- **📥 Download Raw CSV (`/export_csv`):** Downloads a clean, spreadsheet-compatible CSV containing every 5-minute measurement with exact timestamps.
- **ℹ️ About & Docs (`/about`):** Onboard technical manual including vector SVG flow diagrams, hardware pinouts, and regulatory liability documentation.

---

## 📊 Google Sheets Cloud Telemetry Setup

1. Create a new spreadsheet at [sheets.new](https://sheets.new).
2. Go to **Extensions -> Apps Script**.
3. Replace any placeholder script with the following code:

```javascript
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

  // Append new telemetry entry
  sheet.appendRow([date, device, temp, hum, bat, volt, rssi, pwr, note]);

  // Optional: Trigger urgent email alert on limit breach
  if (temp !== "-" && (temp < 2.0 || temp > 8.0)) {
    MailApp.sendEmail(Session.getActiveUser().getEmail(), 
                      "🚨 Cold Chain Alert: " + device, 
                      "Temperature excursion detected!\nCurrent Temperature: " + temp + " °C\nMains Power: " + pwr);
  }
  return ContentService.createTextOutput("OK").setMimeType(ContentService.MimeType.TEXT);
}
```

4. Click **Deploy -> New Deployment**.
5. Select **Web App**:
   - **Execute as:** `Me`
   - **Who has access:** `Anyone`
6. Copy the generated **Web App URL** and paste it into the Thermo_Obs Web Portal under section 6.

---

## 📄 License
This project is released under the **MIT License**. Free for personal, academic, and commercial application.