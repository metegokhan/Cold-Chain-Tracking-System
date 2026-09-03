#pragma once
#include <Arduino.h>

class AboutPageGenerator {
public:
  static String buildAboutHtml() {
    String html = "<!DOCTYPE html><html lang=\"en\"><head><meta charset=\"UTF-8\">";
    html += "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">";
    html += "<title>About Thermo_Obs - System Docs & Hardware Architecture</title>";
    html += "<style>";
    html += "body { font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, sans-serif; background: #f0f2f5; margin: 0; padding: 15px; color: #202124; line-height: 1.6; }";
    html += ".container { max-width: 900px; margin: 0 auto; }";
    html += ".card { background: #fff; border-radius: 12px; padding: 24px; margin-bottom: 20px; box-shadow: 0 2px 8px rgba(0,0,0,0.08); }";
    html += "h1 { color: #1a73e8; margin: 0 0 10px 0; font-size: 26px; }";
    html += "h2 { color: #1a73e8; font-size: 19px; border-bottom: 2px solid #e8f0fe; padding-bottom: 8px; margin-top: 25px; margin-bottom: 12px; }";
    html += "h3 { font-size: 15px; color: #333; margin: 15px 0 6px; }";
    html += "p { margin: 8px 0; font-size: 14px; color: #444; }";
    html += ".nav-bar { display: flex; gap: 10px; margin-bottom: 15px; flex-wrap: wrap; }";
    html += ".btn-nav { background: #1a73e8; color: #fff; padding: 8px 16px; border-radius: 6px; text-decoration: none; font-weight: bold; font-size: 13px; }";
    html += ".btn-nav.gray { background: #5f6368; }";
    html += "table { width: 100%; border-collapse: collapse; margin-top: 10px; font-size: 13px; }";
    html += "th, td { border: 1px solid #dadce0; padding: 10px; text-align: left; }";
    html += "th { background: #f8f9fa; font-weight: 600; color: #202124; }";
    html += ".badge { background: #e8f0fe; color: #1a73e8; padding: 3px 8px; border-radius: 4px; font-size: 12px; font-weight: bold; }";
    html += ".badge-red { background: #fce8e6; color: #d93025; padding: 3px 8px; border-radius: 4px; font-size: 12px; font-weight: bold; }";
    html += ".code-box { background: #282c34; color: #abb2bf; padding: 14px; border-radius: 8px; font-family: monospace; font-size: 13px; overflow-x: auto; margin: 10px 0; }";
    html += ".diagram-box { background: #f8f9fa; border: 1px solid #dadce0; border-radius: 8px; padding: 15px; text-align: center; margin: 15px 0; }";
    html += ".disclaimer { background: #fff8e1; border-left: 5px solid #fbc02d; padding: 15px; border-radius: 6px; font-size: 13px; color: #795548; }";
    html += ".img-board { width: 100%; max-width: 480px; border-radius: 8px; border: 1px solid #dadce0; box-shadow: 0 2px 6px rgba(0,0,0,0.1); margin: 10px 0; }";
    html += ".pin-table th { background: #e8f0fe; color: #1a73e8; }";
    html += "</style>";
    html += "</head><body>";

    html += "<div class=\"container\">";

    // Nav bar
    html += "<div class=\"nav-bar\">";
    html += "  <a href=\"/\" class=\"btn-nav gray\">⬅️ Back to Portal</a>";
    html += "  <a href=\"/report\" target=\"_blank\" class=\"btn-nav\">📄 30-Day Audit Report (PDF)</a>";
    html += "  <a href=\"/export_csv\" class=\"btn-nav\" style=\"background:#34a853;\">📥 Download CSV</a>";
    html += "</div>";

    // Header Card
    html += "<div class=\"card\">";
    html += "  <h1>❄️ Thermo_Obs: Cold Chain Monitoring System</h1>";
    html += "  <p>An enterprise-grade, ultra low-cost cold chain temperature observer, cloud logger, power failure monitor, and visual OLED analytics unit powered by the <strong>ESP32-C3</strong> microcontroller and <strong>Xiaomi Mijia BLE</strong> thermometers.</p>";
    html += "</div>";

    // 1. Hardware Architecture & Pinout
    html += "<div class=\"card\">";
    html += "  <h2>🛠️ 1. Hardware Architecture & Board Specifications</h2>";
    html += "  <p>The system is specifically engineered around the compact <strong>ESP32-C3 0.42\" OLED Development Board</strong> (RISC-V 32-bit single-core, 160MHz with integrated SSD1306 display).</p>";
    html += "  <div style=\"text-align:center;margin:15px 0;\">";
    html += "    <img class=\"img-board\" src=\"https://codey.online/boards/esp32-c3-oled/esp32-c3-oled.png\" alt=\"ESP32-C3 0.42 OLED Board Pinout\" onerror=\"this.style.display='none'\">";
    html += "    <div style=\"font-size:12px;color:#666;\">Board Reference: <a href=\"https://codey.online/boards/esp32-c3-oled\" target=\"_blank\" style=\"color:#1a73e8;text-decoration:none;\">Codey Online - ESP32-C3 OLED 0.42\" Specs & Pinout</a></div>";
    html += "  </div>";

    html += "  <h3>📌 Hardware Pin Assignment Map</h3>";
    html += "  <table class=\"pin-table\"><thead><tr><th>Function</th><th>ESP32-C3 GPIO</th><th>Electrical Details</th></tr></thead><tbody>";
    html += "  <tr><td><strong>I2C OLED Clock (SCL)</strong></td><td><span class=\"badge\">GPIO 6</span></td><td>Built-in SSD1306 0.42\" 128x64 (72x40 visible window)</td></tr>";
    html += "  <tr><td><strong>I2C OLED Data (SDA)</strong></td><td><span class=\"badge\">GPIO 5</span></td><td>On-board hardware I2C pull-ups</td></tr>";
    html += "  <tr><td><strong>BOOT / Navigation Button</strong></td><td><span class=\"badge\">GPIO 9</span></td><td>Internal PULLUP (Active LOW) - Screen nav & 5s Menu</td></tr>";
    html += "  <tr><td><strong>Mains Power Detection</strong></td><td><span class=\"badge\">GPIO 4</span></td><td>Optocoupler / 5V USB sensing input (Configurable Active Low/High)</td></tr>";
    html += "  <tr><td><strong>Thermometer Sensor</strong></td><td><span class=\"badge\">BLE Radio</span></td><td>Xiaomi LYWSD03MMC (ATC / BTHome V2 custom firmware)</td></tr>";
    html += "  </tbody></table>";
    html += "</div>";

    // 2. System Workflow (SVG Diagram)
    html += "<div class=\"card\">";
    html += "  <h2>🔄 2. System Architecture & Data Flow</h2>";
    html += "  <p>Thermo_Obs operates on a resilient non-blocking state machine that balances BLE reception, local analytics, LittleFS durability, and Wi-Fi transmissions:</p>";
    html += "  <div class=\"diagram-box\">";
    
    // Vector SVG Diagram
    html += "<svg viewBox=\"0 0 1000 620\" width=\"100%\" height=\"auto\" xmlns=\"http://www.w3.org/2000/svg\" font-family=\"-apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, sans-serif\">";
    html += "  <defs>";
    html += "    <linearGradient id=\"blueGrad\" x1=\"0%\" y1=\"0%\" x2=\"100%\" y2=\"100%\"><stop offset=\"0%\" stop-color=\"#1a73e8\"/><stop offset=\"100%\" stop-color=\"#0d47a1\"/></linearGradient>";
    html += "    <linearGradient id=\"greenGrad\" x1=\"0%\" y1=\"0%\" x2=\"100%\" y2=\"100%\"><stop offset=\"0%\" stop-color=\"#34a853\"/><stop offset=\"100%\" stop-color=\"#1b5e20\"/></linearGradient>";
    html += "    <linearGradient id=\"redGrad\" x1=\"0%\" y1=\"0%\" x2=\"100%\" y2=\"100%\"><stop offset=\"0%\" stop-color=\"#ea4335\"/><stop offset=\"100%\" stop-color=\"#b71c1c\"/></linearGradient>";
    html += "    <linearGradient id=\"orangeGrad\" x1=\"0%\" y1=\"0%\" x2=\"100%\" y2=\"100%\"><stop offset=\"0%\" stop-color=\"#fb8c00\"/><stop offset=\"100%\" stop-color=\"#e65100\"/></linearGradient>";
    html += "    <linearGradient id=\"purpleGrad\" x1=\"0%\" y1=\"0%\" x2=\"100%\" y2=\"100%\"><stop offset=\"0%\" stop-color=\"#8e24aa\"/><stop offset=\"100%\" stop-color=\"#4a148c\"/></linearGradient>";
    html += "  </defs>";
    html += "  <rect width=\"1000\" height=\"620\" rx=\"14\" fill=\"#f8f9fa\" stroke=\"#dadce0\" stroke-width=\"1\"/>";
    html += "  <text x=\"500\" y=\"38\" text-anchor=\"middle\" font-size=\"18\" font-weight=\"bold\" fill=\"#202124\">Thermo_Obs: End-to-End System Operation &amp; Control Flow</text>";
    html += "  <text x=\"500\" y=\"58\" text-anchor=\"middle\" font-size=\"12\" fill=\"#5f6368\">What it checks • How it decides • Local 30-day LittleFS persistence • Cloud &amp; PDF outputs</text>";
    html += "  <rect x=\"25\" y=\"85\" width=\"200\" height=\"150\" rx=\"10\" fill=\"#fff\" stroke=\"#dadce0\" stroke-width=\"2\"/>";
    html += "  <rect x=\"25\" y=\"85\" width=\"200\" height=\"30\" rx=\"10\" fill=\"url(#blueGrad)\"/>";
    html += "  <text x=\"125\" y=\"105\" text-anchor=\"middle\" font-size=\"13\" font-weight=\"bold\" fill=\"#fff\">📡 Sensor Inputs</text>";
    html += "  <text x=\"40\" y=\"135\" font-size=\"11\" font-weight=\"bold\" fill=\"#202124\">• Xiaomi BLE Beacon:</text>";
    html += "  <text x=\"48\" y=\"152\" font-size=\"10\" fill=\"#5f6368\">Temp, Hum, Bat %, Volt, RSSI</text>";
    html += "  <text x=\"40\" y=\"175\" font-size=\"11\" font-weight=\"bold\" fill=\"#202124\">• GPIO 4 (Mains Power):</text>";
    html += "  <text x=\"48\" y=\"192\" font-size=\"10\" fill=\"#5f6368\">5V / 220V Outage Sensor</text>";
    html += "  <text x=\"40\" y=\"215\" font-size=\"11\" font-weight=\"bold\" fill=\"#202124\">• GPIO 9 (BOOT Button):</text>";
    html += "  <rect x=\"25\" y=\"255\" width=\"200\" height=\"155\" rx=\"10\" fill=\"#fff\" stroke=\"#dadce0\" stroke-width=\"2\"/>";
    html += "  <rect x=\"25\" y=\"255\" width=\"200\" height=\"30\" rx=\"10\" fill=\"url(#orangeGrad)\"/>";
    html += "  <text x=\"125\" y=\"275\" text-anchor=\"middle\" font-size=\"13\" font-weight=\"bold\" fill=\"#fff\">🔘 UI Navigation Logic</text>";
    html += "  <text x=\"38\" y=\"305\" font-size=\"11\" font-weight=\"bold\" fill=\"#d84315\">• Short Press (&lt; 1.5s):</text>";
    html += "  <text x=\"48\" y=\"322\" font-size=\"10\" fill=\"#5f6368\">Cycles 8 screens; pauses BLE</text>";
    html += "  <text x=\"38\" y=\"345\" font-size=\"11\" font-weight=\"bold\" fill=\"#d84315\">• Long Press (&gt; 5.0s):</text>";
    html += "  <text x=\"48\" y=\"362\" font-size=\"10\" fill=\"#5f6368\">Opens Config Menu (WPS, AP)</text>";
    html += "  <text x=\"38\" y=\"385\" font-size=\"11\" font-weight=\"bold\" fill=\"#d84315\">• 10s Inactivity:</text>";
    html += "  <text x=\"48\" y=\"400\" font-size=\"10\" fill=\"#5f6368\">Auto-returns to Main Screen</text>";
    html += "  <rect x=\"260\" y=\"85\" width=\"450\" height=\"495\" rx=\"12\" fill=\"#fff\" stroke=\"#1a73e8\" stroke-width=\"2\"/>";
    html += "  <rect x=\"260\" y=\"85\" width=\"450\" height=\"35\" rx=\"12\" fill=\"url(#blueGrad)\"/>";
    html += "  <text x=\"485\" y=\"108\" text-anchor=\"middle\" font-size=\"15\" font-weight=\"bold\" fill=\"#fff\">🧠 ESP32-C3 State Machine &amp; Local Persistence</text>";
    html += "  <rect x=\"280\" y=\"135\" width=\"410\" height=\"65\" rx=\"8\" fill=\"#e8f0fe\" stroke=\"#1a73e8\" stroke-width=\"1.5\"/>";
    html += "  <text x=\"295\" y=\"155\" font-size=\"12\" font-weight=\"bold\" fill=\"#1a73e8\">STAGE 1: BLE Acquisition (Wi-Fi OFF)</text>";
    html += "  <text x=\"295\" y=\"173\" font-size=\"11\" fill=\"#3c4043\">Captures BTHome V2 beacon packets; decodes float values. Timeout: 185s.</text>";
    html += "  <text x=\"295\" y=\"190\" font-size=\"10\" fill=\"#70757a\">↳ If timeout occurs: activates auto-discovery or submits empty telemetry packet.</text>";
    html += "  <rect x=\"280\" y=\"215\" width=\"410\" height=\"105\" rx=\"8\" fill=\"#e6f4ea\" stroke=\"#34a853\" stroke-width=\"1.5\"/>";
    html += "  <text x=\"295\" y=\"235\" font-size=\"12\" font-weight=\"bold\" fill=\"#2e7d32\">STAGE 2: Dual-Mode Wi-Fi Transmission &amp; Alarm Engine</text>";
    html += "  <text x=\"295\" y=\"253\" font-size=\"11\" fill=\"#3c4043\">• Disconnects BLE radio; connects to Primary / Backup Wi-Fi station.</text>";
    html += "  <text x=\"295\" y=\"270\" font-size=\"11\" fill=\"#3c4043\">• Synchronizes NTP internet clock (GMT+3) for exact timestamps.</text>";
    html += "  <text x=\"295\" y=\"287\" font-size=\"11\" font-weight=\"bold\" fill=\"#c62828\">• Limit Check: If Temp &lt; 2.0°C or &gt; 8.0°C ➔ Dispatches Telegram Alarm!</text>";
    html += "  <text x=\"295\" y=\"304\" font-size=\"11\" font-weight=\"bold\" fill=\"#e65100\">• Power Check: If GPIO 4 reads Outage ➔ Dispatches Mains Loss Alert!</text>";
    html += "  <rect x=\"280\" y=\"335\" width=\"410\" height=\"60\" rx=\"8\" fill=\"#f1f3f4\" stroke=\"#5f6368\" stroke-width=\"1.5\"/>";
    html += "  <text x=\"295\" y=\"355\" font-size=\"12\" font-weight=\"bold\" fill=\"#3c4043\">STAGE 3: Rest Window &amp; Low-Power (millis based)</text>";
    html += "  <text x=\"295\" y=\"373\" font-size=\"11\" fill=\"#5f6368\">Turns Wi-Fi OFF. Waits for user-defined interval (e.g., 60s). Loop never blocks.</text>";
    html += "  <rect x=\"280\" y=\"410\" width=\"410\" height=\"150\" rx=\"8\" fill=\"#fce8e6\" stroke=\"#ea4335\" stroke-width=\"1.5\"/>";
    html += "  <text x=\"295\" y=\"430\" font-size=\"12\" font-weight=\"bold\" fill=\"#c62828\">💾 30-Day Resilient Local Storage Engine</text>";
    html += "  <text x=\"295\" y=\"450\" font-size=\"11\" font-weight=\"bold\" fill=\"#202124\">1. RAM Circular Buffer (Every 5 Minutes):</text>";
    html += "  <text x=\"310\" y=\"467\" font-size=\"10\" fill=\"#5f6368\">Stores 8,640 samples [timestamp + temp * 10]. Fast, zero wear, instant access.</text>";
    html += "  <text x=\"295\" y=\"490\" font-size=\"11\" font-weight=\"bold\" fill=\"#202124\">2. LittleFS Flash Persistence (Every 30 Minutes):</text>";
    html += "  <text x=\"310\" y=\"507\" font-size=\"10\" fill=\"#5f6368\">Flushes RAM history to '/history.bin'. Survives reboots &amp; power losses.</text>";
    html += "  <text x=\"295\" y=\"530\" font-size=\"11\" font-weight=\"bold\" fill=\"#202124\">3. Local Metrics Computation:</text>";
    html += "  <text x=\"310\" y=\"547\" font-size=\"10\" fill=\"#5f6368\">Continuously calculates 30-day Min/Max and duration within ±0.5°C band.</text>";
    html += "  <rect x=\"745\" y=\"85\" width=\"230\" height=\"235\" rx=\"10\" fill=\"#fff\" stroke=\"#dadce0\" stroke-width=\"2\"/>";
    html += "  <rect x=\"745\" y=\"85\" width=\"230\" height=\"30\" rx=\"10\" fill=\"url(#greenGrad)\"/>";
    html += "  <text x=\"860\" y=\"105\" text-anchor=\"middle\" font-size=\"13\" font-weight=\"bold\" fill=\"#fff\">☁️ Cloud &amp; Notifications</text>";
    html += "  <text x=\"760\" y=\"135\" font-size=\"11\" font-weight=\"bold\" fill=\"#202124\">• Google Sheets:</text>";
    html += "  <text x=\"770\" y=\"152\" font-size=\"10\" fill=\"#5f6368\">Full telemetry row append</text>";
    html += "  <text x=\"760\" y=\"175\" font-size=\"11\" font-weight=\"bold\" fill=\"#202124\">• Offline Watchdog (Apps Script):</text>";
    html += "  <text x=\"770\" y=\"192\" font-size=\"10\" fill=\"#5f6368\">Alerts if device silent &gt;10 min</text>";
    html += "  <text x=\"760\" y=\"215\" font-size=\"11\" font-weight=\"bold\" fill=\"#202124\">• Telegram Bot API:</text>";
    html += "  <text x=\"770\" y=\"232\" font-size=\"10\" fill=\"#5f6368\">Urgent threshold alarms</text>";
    html += "  <text x=\"760\" y=\"255\" font-size=\"11\" font-weight=\"bold\" fill=\"#202124\">• Custom Webhook:</text>";
    html += "  <text x=\"770\" y=\"272\" font-size=\"10\" fill=\"#5f6368\">Home Assistant / REST API</text>";
    html += "  <text x=\"760\" y=\"295\" font-size=\"11\" font-weight=\"bold\" fill=\"#202124\">• Recovery Alert:</text>";
    html += "  <text x=\"770\" y=\"310\" font-size=\"10\" fill=\"#5f6368\">Notifies when connection returns</text>";
    html += "  <rect x=\"745\" y=\"335\" width=\"230\" height=\"245\" rx=\"10\" fill=\"#fff\" stroke=\"#dadce0\" stroke-width=\"2\"/>";
    html += "  <rect x=\"745\" y=\"335\" width=\"230\" height=\"30\" rx=\"10\" fill=\"url(#purpleGrad)\"/>";
    html += "  <text x=\"860\" y=\"355\" text-anchor=\"middle\" font-size=\"13\" font-weight=\"bold\" fill=\"#fff\">📄 Local Web Audit Portal</text>";
    html += "  <text x=\"760\" y=\"385\" font-size=\"11\" font-weight=\"bold\" fill=\"#4a148c\">• View / Print PDF (/report):</text>";
    html += "  <text x=\"770\" y=\"402\" font-size=\"10\" fill=\"#5f6368\">Dynamic Chart.js time-series curve</text>";
    html += "  <text x=\"770\" y=\"417\" font-size=\"10\" fill=\"#5f6368\">KPI cards &amp; excursion breach table</text>";
    html += "  <text x=\"770\" y=\"432\" font-size=\"10\" fill=\"#5f6368\">A4 print-ready official report</text>";
    html += "  <text x=\"760\" y=\"455\" font-size=\"11\" font-weight=\"bold\" fill=\"#4a148c\">• Raw CSV Export (/export_csv):</text>";
    html += "  <text x=\"770\" y=\"472\" font-size=\"10\" fill=\"#5f6368\">Instant 30-day raw data download</text>";
    html += "  <text x=\"760\" y=\"495\" font-size=\"11\" font-weight=\"bold\" fill=\"#4a148c\">• Onboard Docs (/about):</text>";
    html += "  <text x=\"770\" y=\"512\" font-size=\"10\" fill=\"#5f6368\">Offline SVG schematic &amp; specs</text>";
    html += "  <text x=\"760\" y=\"535\" font-size=\"11\" font-weight=\"bold\" fill=\"#4a148c\">• SoftAP Web Config:</text>";
    html += "  <text x=\"770\" y=\"552\" font-size=\"10\" fill=\"#5f6368\">192.168.4.1 Wi-Fi &amp; sensor pairing</text>";
    html += "  <line x1=\"225\" y1=\"160\" x2=\"260\" y2=\"160\" stroke=\"#1a73e8\" stroke-width=\"2.5\" stroke-dasharray=\"4\"/>";
    html += "  <line x1=\"225\" y1=\"330\" x2=\"260\" y2=\"330\" stroke=\"#fb8c00\" stroke-width=\"2.5\"/>";
    html += "  <line x1=\"710\" y1=\"200\" x2=\"745\" y2=\"200\" stroke=\"#34a853\" stroke-width=\"2.5\"/>";
    html += "  <line x1=\"710\" y1=\"460\" x2=\"745\" y2=\"460\" stroke=\"#8e24aa\" stroke-width=\"2.5\"/>";
    html += "</svg>";
    html += "  </div>";
    html += "  <h3>📋 Decision Matrix: What It Checks &amp; How It Acts</h3>";
    html += "  <table><thead><tr><th>Condition / Sensor</th><th>Evaluation Criteria</th><th>Automated Actions</th></tr></thead><tbody>";
    html += "  <tr><td><strong>Cold-Chain Temp</strong></td><td>&lt; 2.0°C (Freeze) or &gt; 8.0°C (Warmth)</td><td>Urgent Telegram Alarm, Google Sheets alert note, flagged in /report table</td></tr>";
    html += "  <tr><td><strong>Mains Electricity</strong></td><td>GPIO 4 reads power loss (0V / Outage)</td><td>Instant Mains Power Loss Alert via Telegram &amp; Webhook, switches to battery thresholds</td></tr>";
    html += "  <tr><td><strong>BLE RF Signal</strong></td><td>No packet received for &gt; 185s</td><td>OLED warning pop-up, triggers auto-discovery, forwards signal loss flag to cloud</td></tr>";
    html += "  <tr><td><strong>Wi-Fi Connection</strong></td><td>Primary AP unreachable</td><td>Automatic failover to Backup Wi-Fi; offline buffering in LittleFS if unavailable</td></tr>";
    html += "  <tr><td><strong>Sensor Battery</strong></td><td>CR2032 voltage &lt; 2.50V (&le; 15%)</td><td>Low battery warning on OLED Screen 3, logged in Google Sheets</td></tr>";
    html += "  </tbody></table>";
    html += "</div>";

    // 3. Display Screens & Button Guide
    html += "<div class=\"card\">";
    html += "  <h2>🖥️ 3. OLED Screen Map & Physical Button Controls</h2>";
    html += "  <p>The unit features 8 scrollable screens controlled via the <strong>BOOT button (GPIO 9)</strong>:</p>";
    html += "  <ul>";
    html += "    <li><strong>Short Press:</strong> Cycles sequentially through the 8 information screens. Automatically pauses background scanning so you can review details without flickering.</li>";
    html += "    <li><strong>5-Second Long Press:</strong> Displays an on-screen loading bar and enters the <strong>Setup Menu</strong> (1. WPS PBC, 2. Web Portal, 3. Manual BLE Discover).</li>";
    html += "    <li><strong>10-Second Inactivity:</strong> In menu mode, inactivity triggers a safe soft reboot, saving telemetry beforehand.</li>";
    html += "  </ul>";

    html += "  <table><thead><tr><th>Screen</th><th>Display Header</th><th>Information Rendered</th></tr></thead><tbody>";
    html += "  <tr><td><strong>1. Primary Screen</strong></td><td><code>[ATC_XXXX w:V b:V]</code></td><td>Real-time temperature in large font with Wi-Fi ('w') and BLE ('b') health indicators.</td></tr>";
    html += "  <tr><td><strong>2. Humidity</strong></td><td><code>[HUMIDITY]</code></td><td>Relative Humidity (% RH).</td></tr>";
    html += "  <tr><td><strong>3. Battery Status</strong></td><td><code>[BATTERY]</code></td><td>Coin-cell percentage (%) and live measured battery voltage (V).</td></tr>";
    html += "  <tr><td><strong>4. BLE Signal</strong></td><td><code>[BLE SIGNAL]</code></td><td>Received Signal Strength Indicator (dBm) with link quality.</td></tr>";
    html += "  <tr><td><strong>5. 30d Minimum</strong></td><td><code>[30d MINIMUM]</code></td><td>Lowest temperature in last 30 days and total duration within &plusmn;0.5&deg;C.</td></tr>";
    html += "  <tr><td><strong>6. 30d Maximum</strong></td><td><code>[30d MAXIMUM]</code></td><td>Highest temperature in last 30 days and total duration within &plusmn;0.5&deg;C.</td></tr>";
    html += "  <tr><td><strong>7. Wi-Fi Info</strong></td><td><code>[WIFI INFO]</code></td><td>Connected SSID, connection status, and assigned local IP address.</td></tr>";
    html += "  <tr><td><strong>8. Sensor Details</strong></td><td><code>[BLE SENSOR INFO]</code></td><td>Active thermometer name, Bluetooth MAC address, and RSSI.</td></tr>";
    html += "  </tbody></table>";
    html += "</div>";

    // 4. Installation & Build Guide
    html += "<div class=\"card\">";
    html += "  <h2>📦 4. Compilation & Flashing Instructions</h2>";
    html += "  <p>The firmware is compiled with Arduino ESP32 Core &ge; 3.0.0 using the <strong>Huge APP</strong> partition scheme to accommodate BLE stack, web servers, and LittleFS:</p>";
    html += "  <div class=\"code-box\">";
    html += "# Compile with Arduino CLI<br>";
    html += "arduino-cli compile --fqbn esp32:esp32:esp32c3:CDCOnBoot=cdc,PartitionScheme=huge_app .<br><br>";
    html += "# Flash via Serial USB (e.g. COM4)<br>";
    html += "arduino-cli upload -p COM4 --fqbn esp32:esp32:esp32c3:CDCOnBoot=cdc,PartitionScheme=huge_app .";
    html += "  </div>";
    html += "  <h3>📚 External Libraries & Dependencies</h3>";
    html += "  <table><thead><tr><th>Library</th><th>Author / Source</th><th>Purpose</th></tr></thead><tbody>";
    html += "  <tr><td><strong>U8g2</strong></td><td>olikraus (<a href=\"https://github.com/olikraus/u8g2\" target=\"_blank\">GitHub</a>)</td><td>Monochrome OLED graphics & font rendering engine</td></tr>";
    html += "  <tr><td><strong>LittleFS</strong></td><td>Espressif Systems</td><td>Flash wear-leveling persistent file storage</td></tr>";
    html += "  <tr><td><strong>BLE & WiFi</strong></td><td>Espressif Systems</td><td>Bluetooth Low Energy scanner & dual-mode Wi-Fi stack</td></tr>";
    html += "  <tr><td><strong>Chart.js</strong></td><td>Open Source (<a href=\"https://www.chartjs.org/\" target=\"_blank\">Chart.js CDN</a>)</td><td>Client-side dynamic time-series charts on audit reports</td></tr>";
    html += "  </tbody></table>";
    html += "</div>";

    // 5. Disclaimer & Statement of Liability
    html += "<div class=\"card\">";
    html += "  <h2>⚖️ 5. Disclaimer & Statement of Liability</h2>";
    html += "  <div class=\"disclaimer\">";
    html += "    <strong>Disclaimer of Warranty & Limitation of Liability:</strong><br>";
    html += "    This project is open-source, and the workflows, scripts, or examples provided may not be entirely error-free. The user assumes sole responsibility for any errors, data loss, or other adverse consequences that may arise during the use of this software and associated hardware; the developers and project maintainers assume no liability or responsibility for such damages or losses.<br><br>";
    html += "    This system is provided <em>\"as is\"</em> without warranty of any kind, either express or implied, including but not limited to the implied warranties of merchantability or fitness for a particular pharmaceutical, clinical, vaccine, or food cold chain storage purpose. The operator assumes full responsibility for calibrating hardware, verifying Bluetooth connectivity, testing power loss monitoring circuits, and adhering to applicable national and international regulatory standards (such as WHO PQS, FDA 21 CFR Part 11 guidance, or Ministry of Health cold chain directives).";
    html += "  </div>";
    html += "</div>";

    html += "</div></body></html>";
    return html;
  }
};
