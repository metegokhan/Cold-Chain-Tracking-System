#pragma once
#include <Arduino.h>
#include <time.h>
#include "history_manager.h"
#include "config_manager.h"

extern ConfigManager cfgMgr;

class ReportGenerator {
public:
  static String formatTimestamp(uint32_t ts) {
    if (ts == 0) return "-";
    time_t raw = (time_t)ts;
    struct tm* t = localtime(&raw);
    if (!t) return "-";
    char buf[32];
    snprintf(buf, sizeof(buf), "%02d.%02d.%04d %02d:%02d",
             t->tm_mday, t->tm_mon + 1, t->tm_year + 1900,
             t->tm_hour, t->tm_min);
    return String(buf);
  }

  static String buildPdfReportHtml() {
    float minT = 999.0, maxT = -999.0;
    uint32_t minTime = 0, maxTime = 0;
    long totalTempTimesTen = 0;
    int validCount = 0;

    int lowViolSamples = 0;   // <= 2.0 C
    int highViolSamples = 0;  // >= 8.0 C
    int lowEvents = 0;
    int highEvents = 0;
    bool inLow = false;
    bool inHigh = false;

    // Ordered chronological analysis
    for (int i = 0; i < historyCount; i++) {
      int idx = (historyHead - historyCount + i + HISTORY_SIZE) % HISTORY_SIZE;
      float tempC = tempHistory[idx].temp / 10.0f;
      uint32_t ts = tempHistory[idx].timestamp;

      if (tempHistory[idx].temp <= -9990) continue; // Unsampled slot

      validCount++;
      totalTempTimesTen += tempHistory[idx].temp;

      if (tempC < minT) { minT = tempC; minTime = ts; }
      if (tempC > maxT) { maxT = tempC; maxTime = ts; }

      // Thresholds: <= 2.0 C or >= 8.0 C
      if (tempC <= 2.0f) {
        lowViolSamples++;
        if (!inLow) { inLow = true; lowEvents++; }
      } else {
        inLow = false;
      }

      if (tempC >= 8.0f) {
        highViolSamples++;
        if (!inHigh) { inHigh = true; highEvents++; }
      } else {
        inHigh = false;
      }
    }

    float avgT = validCount > 0 ? (totalTempTimesTen / (float)validCount) / 10.0f : 0.0f;
    if (minT > 900.0f) minT = 0.0f;
    if (maxT < -900.0f) maxT = 0.0f;

    float lowViolHours = (lowViolSamples * 5.0f) / 60.0f;
    float highViolHours = (highViolSamples * 5.0f) / 60.0f;

    String html = "<!DOCTYPE html><html lang=\"en\"><head><meta charset=\"UTF-8\">";
    html += "<title>Cold Chain 30-Day Audit Report</title>";
    html += "<script src=\"https://cdn.jsdelivr.net/npm/chart.js\"></script>";
    html += "<style>";
    html += "@page { size: A4 portrait; margin: 12mm; }";
    html += "body { font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, sans-serif; color: #202124; background: #f8f9fa; margin: 0; padding: 15px; }";
    html += ".report-container { max-width: 960px; margin: 0 auto; background: #fff; padding: 25px; border-radius: 8px; box-shadow: 0 1px 4px rgba(0,0,0,0.1); }";
    html += ".header { display: flex; justify-content: space-between; align-items: flex-start; border-bottom: 2px solid #1a73e8; padding-bottom: 15px; margin-bottom: 20px; }";
    html += ".title { font-size: 22px; font-weight: bold; color: #1a73e8; margin: 0 0 5px 0; }";
    html += ".meta { font-size: 13px; color: #5f6368; line-height: 1.5; }";
    html += ".kpi-grid { display: grid; grid-template-columns: repeat(4, 1fr); gap: 12px; margin-bottom: 20px; }";
    html += ".kpi { border: 1px solid #dadce0; border-radius: 8px; padding: 12px; text-align: center; }";
    html += ".kpi.freeze { border-color: #1a73e8; background: #e8f0fe; }";
    html += ".kpi.heat { border-color: #d93025; background: #fce8e6; }";
    html += ".kpi.safe { border-color: #34a853; background: #e6f4ea; }";
    html += ".kpi-val { font-size: 22px; font-weight: bold; margin: 5px 0; }";
    html += ".kpi-lbl { font-size: 11px; font-weight: bold; text-transform: uppercase; color: #5f6368; }";
    html += ".kpi-sub { font-size: 11px; color: #70757a; }";
    html += ".chart-box { border: 1px solid #dadce0; border-radius: 8px; padding: 15px; margin-bottom: 20px; background: #fff; height: 320px; }";
    html += ".sec-title { font-size: 16px; font-weight: bold; margin: 20px 0 10px 0; color: #202124; border-bottom: 1px solid #dadce0; padding-bottom: 5px; }";
    html += "table { width: 100%; border-collapse: collapse; font-size: 12px; margin-top: 10px; }";
    html += "th, td { border: 1px solid #dadce0; padding: 8px 10px; text-align: left; }";
    html += "th { background: #f1f3f4; font-weight: 600; }";
    html += ".badge-red { background: #fce8e6; color: #d93025; padding: 2px 6px; border-radius: 4px; font-weight: bold; }";
    html += ".badge-blue { background: #e8f0fe; color: #1a73e8; padding: 2px 6px; border-radius: 4px; font-weight: bold; }";
    html += ".no-print { margin-bottom: 15px; display: flex; gap: 10px; }";
    html += ".btn { background: #1a73e8; color: #fff; border: none; padding: 10px 18px; border-radius: 6px; font-weight: bold; cursor: pointer; text-decoration: none; font-size: 14px; }";
    html += "@media print { body { background: #fff; padding: 0; } .report-container { box-shadow: none; padding: 0; } .no-print { display: none; } }";
    html += "</style>";
    html += "</head><body>";

    html += "<div class=\"report-container\">";
    html += "<div class=\"no-print\">";
    html += "  <button onclick=\"window.print()\" class=\"btn\">🖨️ Print / Save as PDF</button>";
    html += "  <a href=\"/\" class=\"btn\" style=\"background:#5f6368;\">⬅️ Back to Portal</a>";
    html += "</div>";

    html += "<div class=\"header\">";
    html += "  <div>";
    html += "    <h1 class=\"title\">❄️ Cold Chain Temperature Audit Report</h1>";
    html += "    <div class=\"meta\">Target Standard: <strong>+2.0 &deg;C to +8.0 &deg;C</strong> | Rolling Window: <strong>Last 30 Days</strong></div>";
    html += "  </div>";
    html += "  <div class=\"meta\" style=\"text-align:right;\">";
    html += "    <div>Device: <strong>" + cfgMgr.config.bleTargetName + "</strong> (" + cfgMgr.config.bleTargetMac + ")</div>";
    html += "    <div>Generated: <strong>" + formatTimestamp((uint32_t)time(nullptr)) + "</strong></div>";
    html += "    <div>Total Samples: <strong>" + String(validCount) + " (5 min interval)</strong></div>";
    html += "  </div>";
    html += "</div>";

    html += "<div class=\"kpi-grid\">";
    html += "  <div class=\"kpi " + String(minT < 2.0f ? "freeze" : "safe") + "\">";
    html += "    <div class=\"kpi-lbl\">30d Minimum</div>";
    html += "    <div class=\"kpi-val\">" + String(minT, 1) + " &deg;C</div>";
    html += "    <div class=\"kpi-sub\">" + formatTimestamp(minTime) + "</div>";
    html += "  </div>";
    html += "  <div class=\"kpi " + String(maxT > 8.0f ? "heat" : "safe") + "\">";
    html += "    <div class=\"kpi-lbl\">30d Maximum</div>";
    html += "    <div class=\"kpi-val\">" + String(maxT, 1) + " &deg;C</div>";
    html += "    <div class=\"kpi-sub\">" + formatTimestamp(maxTime) + "</div>";
    html += "  </div>";
    html += "  <div class=\"kpi safe\">";
    html += "    <div class=\"kpi-lbl\">Average Temp</div>";
    html += "    <div class=\"kpi-val\">" + String(avgT, 1) + " &deg;C</div>";
    html += "    <div class=\"kpi-sub\">Within Safe Band</div>";
    html += "  </div>";
    html += "  <div class=\"kpi " + String((lowViolSamples + highViolSamples) > 0 ? "heat" : "safe") + "\">";
    html += "    <div class=\"kpi-lbl\">Total Excursion Duration</div>";
    html += "    <div class=\"kpi-val\">" + String(lowViolHours + highViolHours, 1) + " hrs</div>";
    html += "    <div class=\"kpi-sub\">" + String(lowEvents + highEvents) + " violation episodes</div>";
    html += "  </div>";
    html += "</div>";

    html += "<div class=\"sec-title\">📈 30-Day Temperature Profile (Time Series)</div>";
    html += "<div class=\"chart-box\"><canvas id=\"tempChart\"></canvas></div>";

    html += "<div class=\"sec-title\">⚠️ Alarm & Excursion Analytics (&le; 2.0&deg;C or &ge; 8.0&deg;C)</div>";
    html += "<table><thead><tr>";
    html += "<th>Excursion Type</th><th>Threshold</th><th>Events Count</th><th>Cumulative Duration</th><th>Status</th>";
    html += "</tr></thead><tbody>";
    html += "<tr>";
    html += "  <td><span class=\"badge-blue\">FREEZE RISK</span></td>";
    html += "  <td>&le; 2.0 &deg;C</td>";
    html += "  <td>" + String(lowEvents) + "</td>";
    html += "  <td>" + String(lowViolHours, 1) + " hours (" + String(lowViolSamples * 5) + " min)</td>";
    html += "  <td>" + String(lowEvents > 0 ? "<span style=\"color:#1a73e8;font-weight:bold;\">VIOLATION DETECTED</span>" : "Compliant (0 hrs)") + "</td>";
    html += "</tr>";
    html += "<tr>";
    html += "  <td><span class=\"badge-red\">WARMTH EXCURSION</span></td>";
    html += "  <td>&ge; 8.0 &deg;C</td>";
    html += "  <td>" + String(highEvents) + "</td>";
    html += "  <td>" + String(highViolHours, 1) + " hours (" + String(highViolSamples * 5) + " min)</td>";
    html += "  <td>" + String(highEvents > 0 ? "<span style=\"color:#d93025;font-weight:bold;\">VIOLATION DETECTED</span>" : "Compliant (0 hrs)") + "</td>";
    html += "</tr>";
    html += "</tbody></table>";

    // Chart.js Data injection
    html += "<script>";
    html += "const labels = [];";
    html += "const temps = [];";
    
    // Sample down if needed or stream every N points for smooth render
    int step = (validCount > 1000) ? 2 : 1; 
    for (int i = 0; i < historyCount; i += step) {
      int idx = (historyHead - historyCount + i + HISTORY_SIZE) % HISTORY_SIZE;
      if (tempHistory[idx].temp <= -9990) continue;
      float tVal = tempHistory[idx].temp / 10.0f;
      html += "labels.push('" + formatTimestamp(tempHistory[idx].timestamp) + "');";
      html += "temps.push(" + String(tVal, 1) + ");";
    }

    html += "const ctx = document.getElementById('tempChart').getContext('2d');";
    html += "new Chart(ctx, {";
    html += "  type: 'line',";
    html += "  data: {";
    html += "    labels: labels,";
    html += "    datasets: [{";
    html += "      label: 'Temperature (C)',";
    html += "      data: temps,";
    html += "      borderColor: '#1a73e8',";
    html += "      backgroundColor: 'rgba(26, 115, 232, 0.05)',";
    html += "      borderWidth: 1.5,";
    html += "      pointRadius: 0,";
    html += "      fill: true,";
    html += "      tension: 0.1";
    html += "    }]";
    html += "  },";
    html += "  options: {";
    html += "    responsive: true,";
    html += "    maintainAspectRatio: false,";
    html += "    scales: {";
    html += "      y: {";
    html += "        title: { display: true, text: 'Temperature (C)' },";
    html += "        suggestedMin: 0,";
    html += "        suggestedMax: 10";
    html += "      },";
    html += "      x: {";
    html += "        ticks: { maxTicksLimit: 10 }";
    html += "      }";
    html += "    }";
    html += "  }";
    html += "});";
    html += "</script>";

    html += "</div></body></html>";
    return html;
  }
};
