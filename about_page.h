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
    html += "<svg viewBox=\"0 0 800 240\" width=\"100%\" height=\"200\" xmlns=\"http://www.w3.org/2000/svg\">";
    html += "  <defs>";
    html += "    <linearGradient id=\"g1\" x1=\"0%\" y1=\"0%\" x2=\"100%\" y2=\"100%\"><stop offset=\"0%\" stop-color=\"#1a73e8\"/><stop offset=\"100%\" stop-color=\"#0d47a1\"/></linearGradient>";
    html += "    <linearGradient id=\"g2\" x1=\"0%\" y1=\"0%\" x2=\"100%\" y2=\"100%\"><stop offset=\"0%\" stop-color=\"#34a853\"/><stop offset=\"100%\" stop-color=\"#1b5e20\"/></linearGradient>";
    html += "    <linearGradient id=\"g3\" x1=\"0%\" y1=\"0%\" x2=\"100%\" y2=\"100%\"><stop offset=\"0%\" stop-color=\"#f2994a\"/><stop offset=\"100%\" stop-color=\"#e65100\"/></linearGradient>";
    html += "    <linearGradient id=\"g4\" x1=\"0%\" y1=\"0%\" x2=\"100%\" y2=\"100%\"><stop offset=\"0%\" stop-color=\"#9c27b0\"/><stop offset=\"100%\" stop-color=\"#4a148c\"/></linearGradient>";
    html += "  </defs>";
    // Boxes
    html += "  <rect x=\"20\" y=\"80\" width=\"150\" height=\"80\" rx=\"10\" fill=\"url(#g1)\"/>";
    html += "  <text x=\"95\" y=\"115\" fill=\"#fff\" font-weight=\"bold\" font-size=\"13\" text-anchor=\"middle\">Xiaomi Sensor</text>";
    html += "  <text x=\"95\" y=\"135\" fill=\"#e8f0fe\" font-size=\"11\" text-anchor=\"middle\">BTHome V2 BLE Beacon</text>";

    html += "  <line x1=\"170\" y1=\"120\" x2=\"220\" y2=\"120\" stroke=\"#1a73e8\" stroke-width=\"3\" stroke-dasharray=\"4\"/>";
    html += "  <polygon points=\"220,115 230,120 220,125\" fill=\"#1a73e8\"/>";

    html += "  <rect x=\"230\" y=\"45\" width=\"190\" height=\"150\" rx=\"12\" fill=\"#fff\" stroke=\"#1a73e8\" stroke-width=\"2\"/>";
    html += "  <text x=\"325\" y=\"72\" fill=\"#1a73e8\" font-weight=\"bold\" font-size=\"14\" text-anchor=\"middle\">ESP32-C3 Engine</text>";
    html += "  <rect x=\"245\" y=\"85\" width=\"160\" height=\"30\" rx=\"5\" fill=\"#e8f0fe\"/>";
    html += "  <text x=\"325\" y=\"105\" fill=\"#1a73e8\" font-size=\"11\" text-anchor=\"middle\">RAM Buffer (8640 samples)</text>";
    html += "  <rect x=\"245\" y=\"122\" width=\"160\" height=\"30\" rx=\"5\" fill=\"#fce8e6\"/>";
    html += "  <text x=\"325\" y=\"142\" fill=\"#d93025\" font-size=\"11\" text-anchor=\"middle\">LittleFS Wear-Leveling (30m)</text>";
    html += "  <text x=\"325\" y=\"175\" fill=\"#5f6368\" font-size=\"10\" text-anchor=\"middle\">NTP Internet Clock GMT+3</text>";

    html += "  <line x1=\"420\" y1=\"100\" x2=\"480\" y2=\"75\" stroke=\"#34a853\" stroke-width=\"3\"/>";
    html += "  <polygon points=\"480,70 488,75 480,82\" fill=\"#34a853\"/>";
    html += "  <rect x=\"490\" y=\"35\" width=\"180\" height=\"70\" rx=\"10\" fill=\"url(#g2)\"/>";
    html += "  <text x=\"580\" y=\"65\" fill=\"#fff\" font-weight=\"bold\" font-size=\"13\" text-anchor=\"middle\">Cloud & Notifications</text>";
    html += "  <text x=\"580\" y=\"85\" fill=\"#e6f4ea\" font-size=\"11\" text-anchor=\"middle\">Sheets / Telegram / Webhook</text>";

    html += "  <line x1=\"420\" y1=\"140\" x2=\"480\" y2=\"165\" stroke=\"#9c27b0\" stroke-width=\"3\"/>";
    html += "  <polygon points=\"480,160 488,165 480,172\" fill=\"#9c27b0\"/>";
    html += "  <rect x=\"490\" y=\"130\" width=\"180\" height=\"70\" rx=\"10\" fill=\"url(#g4)\"/>";
    html += "  <text x=\"580\" y=\"160\" fill=\"#fff\" font-weight=\"bold\" font-size=\"13\" text-anchor=\"middle\">Web Audit Portal</text>";
    html += "  <text x=\"580\" y=\"180\" fill=\"#f3e5f5\" font-size=\"11\" text-anchor=\"middle\">Interactive PDF & CSV Export</text>";
    html += "</svg>";

    html += "  </div>";
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
