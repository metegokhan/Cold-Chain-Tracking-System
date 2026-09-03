/**
 * ============================================================================
 * Thermo_Obs - Google Sheets Telemetry & Alert Webhook
 * ============================================================================
 * Features:
 *  - Automatic tabular header initialization
 *  - Real-time temperature threshold breach alert emails
 *  - Mains power outage detection & alerting
 *  - Automated offline detection trigger (heartbeat watchdog)
 *  - Automatic recovery notification when device re-establishes connection
 * ============================================================================
 */

// ==========================================
// --- CONFIGURATION ---
// ==========================================
const EMAIL_RECIPIENT = "mete.gokhan@gmail.com"; // Notification recipient email
const MIN_TEMP_LIMIT = 2.5;                      // Lower safe temperature limit (°C)
const MAX_TEMP_LIMIT = 7.5;                      // Upper safe temperature limit (°C)
const OFFLINE_TIMEOUT_MINUTES = 10;              // Inactivity timeout before triggering offline alert

/**
 * Handle incoming GET requests from ESP32-C3
 */
function doGet(e) {
  return handleData(e);
}

/**
 * Handle incoming POST requests from ESP32-C3 / Webhook
 */
function doPost(e) {
  return handleData(e);
}

/**
 * Ensure table headers are present on sheet creation
 */
function ensureHeaders(sheet) {
  if (sheet.getLastRow() === 0) {
    sheet.appendRow([
      "Date & Time",
      "Device Name",
      "Temp (°C)",
      "Humidity (%)",
      "Battery (%)",
      "Voltage (V)",
      "RSSI (dBm)",
      "Power Status",
      "Event Note"
    ]);
    sheet.getRange(1, 1, 1, 9).setFontWeight("bold").setBackground("#e8f0fe");
  }
}

/**
 * Primary telemetry processing and logging routine
 */
function handleData(e) {
  try {
    const sheet = SpreadsheetApp.getActiveSpreadsheet().getActiveSheet();
    ensureHeaders(sheet);

    const params = (e && e.parameter) ? e.parameter : {};
    const now = new Date();
    const dateFormatted = Utilities.formatDate(now, "GMT+3", "dd.MM.yyyy HH:mm:ss");

    // Parse and sanitize incoming parameters
    const device = params.device || "ATC_UNKNOWN";
    const temp = (params.temp === "-" || isNaN(parseFloat(params.temp))) ? "-" : parseFloat(params.temp);
    const hum = (params.hum === "-" || isNaN(parseFloat(params.hum))) ? "-" : parseFloat(params.hum);
    const bat = (params.bat === "-" || isNaN(parseInt(params.bat))) ? "-" : parseInt(params.bat);
    const volt = (params.volt === "-" || isNaN(parseFloat(params.volt))) ? "-" : parseFloat(params.volt);
    const rssi = params.rssi || "-";
    const pwr = params.pwr || "ONLINE";
    let note = params.note || "Normal";

    // 1. Temperature Excursion Check (Cold Chain Breach)
    if (temp !== "-") {
      if (temp < MIN_TEMP_LIMIT || temp > MAX_TEMP_LIMIT) {
        note = "LIMIT BREACH!";
        sendAlertEmail(
          `⚠️ TEMPERATURE ALERT: ${device}`,
          `Attention!\n\n` +
          `Temperature reading from ${device} breached defined safety thresholds:\n\n` +
          `• Temperature: ${temp} °C (Allowed: ${MIN_TEMP_LIMIT}°C - ${MAX_TEMP_LIMIT}°C)\n` +
          `• Humidity: %${hum} RH\n` +
          `• Power State: ${pwr}\n` +
          `• Battery: %${bat} (${volt}V)\n` +
          `• Signal (RSSI): ${rssi} dBm\n` +
          `• Timestamp: ${dateFormatted}\n\n` +
          `Please inspect cold-room storage immediately.`
        );
      }
    }

    // 2. Mains Power Disconnection Check
    if (pwr === "OUTAGE") {
      note = (note === "Normal") ? "POWER OUTAGE" : note + " & POWER OUTAGE";
      sendAlertEmail(
        `🚨 MAINS POWER OUTAGE: ${device}`,
        `CRITICAL ALERT!\n\n` +
        `Mains electricity disconnection detected for ${device}.\n` +
        `System is currently operating on battery power.\n\n` +
        `• Current Temp: ${temp} °C\n` +
        `• Battery: %${bat} (${volt}V)\n` +
        `• Timestamp: ${dateFormatted}`
      );
    }

    // 3. Append Data Row (Date, Device, Temp, Hum, Bat, Volt, RSSI, Power, Note)
    sheet.appendRow([
      dateFormatted,
      device,
      temp,
      hum,
      bat,
      volt,
      rssi,
      pwr,
      note
    ]);

    // 4. Connection Restored Check (If device was previously flagged offline)
    const wasOffline = PropertiesService.getScriptProperties().getProperty("OFFLINE_ALERTED");
    if (wasOffline === "true") {
      sendAlertEmail(
        `✅ CONNECTION RESTORED: ${device}`,
        `Notice:\n\n` +
        `Data transmission has resumed normally for ${device}.\n\n` +
        `• Current Temp: ${temp} °C\n` +
        `• Power State: ${pwr}\n` +
        `• Resumed at: ${dateFormatted}`
      );
    }

    // Update heartbeat tracking
    PropertiesService.getScriptProperties().setProperty("LAST_SEEN", now.getTime().toString());
    PropertiesService.getScriptProperties().setProperty("OFFLINE_ALERTED", "false");

    return ContentService.createTextOutput("OK").setMimeType(ContentService.MimeType.TEXT);
  } catch (error) {
    return ContentService.createTextOutput("ERROR: " + error.toString()).setMimeType(ContentService.MimeType.TEXT);
  }
}

/**
 * Offline Watchdog - Call this via a Google Apps Script Time-Driven Trigger
 * Recommended trigger: Every 5 or 10 minutes
 */
function checkDeviceOffline() {
  const lastSeenStr = PropertiesService.getScriptProperties().getProperty("LAST_SEEN");
  const offlineAlerted = PropertiesService.getScriptProperties().getProperty("OFFLINE_ALERTED");

  if (!lastSeenStr) return; // Skip if no telemetry has been received yet

  const lastSeenTime = parseInt(lastSeenStr);
  const now = new Date().getTime();
  const diffMinutes = (now - lastSeenTime) / (1000 * 60);

  // If elapsed time exceeds timeout and no alert has been dispatched yet
  if (diffMinutes >= OFFLINE_TIMEOUT_MINUTES && offlineAlerted !== "true") {
    sendAlertEmail(
      `🚨 DEVICE OFFLINE / TELEMETRY LOSS!`,
      `WARNING: No telemetry received from ESP32 / BLE sensor for ${Math.round(diffMinutes)} minutes!\n\n` +
      `Possible root causes:\n` +
      `- Mains power outage or battery drained\n` +
      `- Wi-Fi router disconnection or internet outage\n` +
      `- BLE thermometer battery (CR2032) exhausted\n\n` +
      `Please check the cold chain storage unit immediately.`
    );
    PropertiesService.getScriptProperties().setProperty("OFFLINE_ALERTED", "true");
  }
}

/**
 * Dispatch notification email via Google MailApp service
 */
function sendAlertEmail(subject, body) {
  try {
    MailApp.sendEmail(EMAIL_RECIPIENT, subject, body);
  } catch (e) {
    console.error("Failed to send alert email: " + e.toString());
  }
}
