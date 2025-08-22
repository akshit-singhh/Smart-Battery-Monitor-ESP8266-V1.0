#include "AppServer.h"
#include "EEPROMUtils.h"
#include <ArduinoJson.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <time.h>
#include <RTClib.h>
extern RTC_DS3231 rtc;

#define MAX_LOG_LINES 50
String serialLogBuffer[MAX_LOG_LINES];
int logIndex = 0;

extern bool rtc_present;
void addSerialLog(const String& message) {
  String timePart;
  if (rtc_present) {
    DateTime now = rtc.now(); // Already in IST from NTP sync
    char buf[30];
    int hour12 = now.hour() % 12;
    if (hour12 == 0) hour12 = 12;
    const char* ampm = (now.hour() >= 12) ? "PM" : "AM";
    sprintf(buf, "%04d-%02d-%02d %02d:%02d:%02d %s",
            now.year(), now.month(), now.day(),
            hour12, now.minute(), now.second(), ampm);
    timePart = String(buf);
  } else {
    timePart = "RTC-N/A";
  }

  unsigned long seconds = millis() / 1000;
  int days = seconds / 86400;
  seconds %= 86400;
  int hours = seconds / 3600;
  seconds %= 3600;
  int minutes = seconds / 60;
  seconds %= 60;

  char uptimeBuf[20];
  sprintf(uptimeBuf, "%02dd:%02dh:%02dm:%02ds", days, hours, minutes, seconds);

  String logLine = "[" + String(uptimeBuf) + "] [" + timePart + "] " + message;
  serialLogBuffer[logIndex] = logLine;
  logIndex = (logIndex + 1) % MAX_LOG_LINES;
  Serial.println(logLine);
}

void syncTimeFromNTPtoRTC() {
  if (WiFi.status() != WL_CONNECTED) {
    addSerialLog("❌ WiFi not connected. Cannot sync time.");
    return;
  }

  configTime("IST-5:30",
    "0.in.pool.ntp.org",
    "1.in.pool.ntp.org",
    "pool.ntp.org"
  );

  Serial.print("Waiting for NTP time sync");

  time_t now = time(nullptr);
  unsigned long startMs = millis();

  while (now < 946684800 && millis() - startMs < 10000) {
    delay(500);
    Serial.print(".");
    now = time(nullptr);
  }
  Serial.println();

  if (now >= 946684800) {
    struct tm timeinfo;
    if (!localtime_r(&now, &timeinfo)) {
      addSerialLog("❌ Failed to convert time.");
      return;
    }

    DateTime ntpTime(
      timeinfo.tm_year + 1900,
      timeinfo.tm_mon + 1,
      timeinfo.tm_mday,
      timeinfo.tm_hour,
      timeinfo.tm_min,
      timeinfo.tm_sec
    );

    rtc.adjust(ntpTime);

    char formatted[40];
    strftime(formatted, sizeof(formatted), "%Y-%m-%d %I:%M:%S %p", &timeinfo);
    addSerialLog("✅ RTC updated via NTP (IST): " + String(formatted));
  } else {
    addSerialLog("❌ NTP sync failed. RTC not updated.");
  }
}


// Server
ESP8266WebServer server(80);

// External variables
extern float currentVoltage;
extern float filteredCurrent;
extern float currentPower;
extern float soc;
extern float batteryCapacityAh;
extern float voltageOffset;
extern float currentOffset;
extern float mVperAmp;
extern float chargingCurrentThreshold;
extern float dischargingCurrentThreshold;
extern float totalCoulombs;
extern unsigned long lastActiveStateChange;
extern float currentDeadzoneThreshold;


const uint16_t ADDR_WIFI_SSID = 500;
const uint16_t ADDR_WIFI_PASS = 564;
const uint16_t ADDR_BATTERY_CAPACITY = 20;
const uint16_t ADDR_VOLTAGE_OFFSET = 30;
const uint16_t ADDR_CURRENT_OFFSET = 40;
const uint16_t ADDR_MV_PER_AMP = 50;
const uint16_t ADDR_CHARGING_THRESHOLD = 200;
const uint16_t ADDR_DISCHARGING_THRESHOLD = 210;
const uint16_t ADDR_SOC = 140; // Same as in menubased.ino
const uint16_t ADDR_CURRENT_DEADZONE = 220; // float → uses 4 bytes: 220–223


char savedSsid[32] = "";
char savedPass[64] = "";

// --------------------------- Forward declarations ---------------------------
// These are used by setupServerRoutes_AP() which appears before the functions'
// definitions in the file. Declaring them here prevents "not declared" errors.
void handleWiFiConfigPage();
void handleWiFiConfig();
void handleNotFound();

void handleSettingsGet();
void handleSettingsPost();
// ---------------------------------------------------------------------------

// ========================= Routes ============================

void handleLiveData() {
  StaticJsonDocument<256> doc;
  doc["voltage"] = currentVoltage;
  doc["current"] = filteredCurrent;
  doc["soc"] = soc;
  doc["power"] = currentPower;
  doc["runtime"] = "N/A";
  doc["status"] = (filteredCurrent > chargingCurrentThreshold) ? "Charging" :
                  ((filteredCurrent < -dischargingCurrentThreshold) ? "Discharging" : "Idle");
  doc["rssi"] = WiFi.RSSI();

  // ✅ Add mode field
  if (WiFi.getMode() == WIFI_AP) {
    doc["mode"] = "AP";
  } else if (WiFi.getMode() == WIFI_STA) {
    doc["mode"] = "STA";
  } else if (WiFi.getMode() == WIFI_AP_STA) {
    if (WiFi.status() == WL_CONNECTED) doc["mode"] = "STA";
    else doc["mode"] = "AP";
  } else {
    doc["mode"] = "NONE";
  }

  // ✅ Add IP address
  if (WiFi.getMode() == WIFI_AP) {
    doc["ip"] = WiFi.softAPIP().toString();
  } else {
    doc["ip"] = WiFi.localIP().toString();
  }

  String jsonStr;
  serializeJson(doc, jsonStr);
  server.send(200, "application/json", jsonStr);
}

void handleServerClient() {
  server.handleClient();
}

// AP Mode server routes
void setupServerRoutes_AP(const String& apIP, const String& apSsid, const String& apPass);

void handleAPMenu(const String& apIP, const String& apSsid, const String& apPass) {
  String html;
  html += "<!DOCTYPE html><html><head><title>AP Mode Menu</title>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
  html += "<style>";
  html += "body{font-family: Arial; text-align:center; background-color:#f4f4f4;}";
  html += "h2{color:#333;}";
  html += "button{padding:10px 20px; margin:10px; font-size:16px;}";
  html += ".card{background:white; padding:15px; margin:15px; border-radius:10px; box-shadow:0 2px 5px rgba(0,0,0,0.2);}";
  html += "</style></head><body>";

  html += "<h2>📶 AP Mode - Setup</h2>";
  html += "<div class='card'>";
  html += "<p><b>SSID:</b> " + apSsid + "</p>";
  html += "<p><b>Password:</b> " + apPass + "</p>";
  html += "<p><b>IP:</b> " + apIP + "</p>";
  html += "</div>";

  html += "<p>Select an option below:</p>";
  html += "<a href='/ap_details'><button>1 AP Details</button></a><br>";
  html += "<a href='/ap_qr'><button>2 QR Code</button></a>";

  html += "</body></html>";
  server.send(200, "text/html", html);
}


void handleAPDetails(const String& apIP, const String& apSsid, const String& apPass) {
  String html = "<html><head><title>AP Details</title></head><body style='font-family: Arial; text-align:center;'>";
  html += "<h2>AP Details</h2>";
  html += "<p><b>SSID:</b> " + apSsid + "</p>";
  html += "<p><b>Password:</b> " + apPass + "</p>";
  html += "<p><b>IP Address:</b> " + apIP + "</p>";
  html += "<a href='/'><button>Back</button></a>";
  html += "</body></html>";
  server.send(200, "text/html", html);
}

void handleAPQR(const String& apIP) {
  // QR now contains the WiFi config page link
  String qrData = "http://" + apIP + "/wifi_config";
  String qrUrl = "https://api.qrserver.com/v1/create-qr-code/?size=200x200&data=" + qrData;

  String html = "<html><head><title>AP QR Code</title></head>"
                "<body style='font-family: Arial; text-align:center;'>";
  html += "<h2>Scan to Configure ESP WiFi</h2>";
  html += "<img src='" + qrUrl + "' alt='QR Code'><br>";
  html += "<p>URL: " + qrData + "</p>";
  html += "<a href='/'><button>Back</button></a>";
  html += "</body></html>";
  
  server.send(200, "text/html", html);
}


void setupServerRoutes_AP(const String& apIP, const String& apSsid, const String& apPass) {
  // === Main AP Menu Page ===
  server.on("/", HTTP_GET, [apIP, apSsid, apPass]() {
    handleAPMenu(apIP, apSsid, apPass);
  });

  server.on("/sta_ip", HTTP_GET, []() {
    if (WiFi.status() == WL_CONNECTED) {
      server.send(200, "text/plain", WiFi.localIP().toString());
    } else {
      server.send(200, "text/plain", "NOT_CONNECTED");
    }
  });

  server.on("/reboot", HTTP_POST, []() {
    server.send(200, "text/plain", "Rebooting...");
    addSerialLog("Reboot command received via API.");
    delay(500);
    ESP.restart();
  });

  // === AP Details Page ===
  server.on("/ap_details", HTTP_GET, [apIP, apSsid, apPass]() {
    handleAPDetails(apIP, apSsid, apPass);
  });

  // === QR Code Page ===
  server.on("/ap_qr", HTTP_GET, [apIP]() {
    handleAPQR(apIP);
  });

  // === WiFi Config Page (GET & POST) ===
  server.on("/wifi_config", HTTP_GET, handleWiFiConfigPage);
  server.on("/wifi_config", HTTP_POST, handleWiFiConfig);

  // === Live Data in AP mode (now shows real readings) ===
  server.on("/live_data", HTTP_GET, [apSsid, apIP]() {
    StaticJsonDocument<256> doc;
    doc["voltage"] = currentVoltage;
    doc["current"] = filteredCurrent;
    doc["soc"] = soc;
    doc["power"] = currentPower;
    doc["runtime"] = "N/A";
    doc["status"] = (filteredCurrent > chargingCurrentThreshold) ? "Charging" :
                    ((filteredCurrent < -dischargingCurrentThreshold) ? "Discharging" : "Idle");
    doc["rssi"] = WiFi.RSSI();
    doc["mode"] = "AP";
    doc["ip"] = apIP;
    String jsonStr;
    serializeJson(doc, jsonStr);
    server.send(200, "application/json", jsonStr);
  });

  // ✅ Serial log in AP mode
  server.on("/serial_log", HTTP_GET, []() {
    String logContent;
    int idx = logIndex;
    for (int i = 0; i < MAX_LOG_LINES; i++) {
      logContent += serialLogBuffer[idx] + "\n";
      idx = (idx + 1) % MAX_LOG_LINES;
    }
    server.send(200, "text/plain", logContent);
  });

  // ✅ Unified settings endpoint for AP mode
  server.on("/settings", HTTP_GET, handleSettingsGet);
  server.on("/settings", HTTP_POST, handleSettingsPost);

  // === 404 Handler ===
  server.onNotFound(handleNotFound);
}



void handleSettingsGet() {
  StaticJsonDocument<256> doc;
  doc["capacity_ah"] = batteryCapacityAh;
  doc["voltage_offset"] = voltageOffset;
  doc["current_offset"] = currentOffset;
  doc["mv_per_amp"] = mVperAmp;
  doc["charge_threshold"] = chargingCurrentThreshold;
  doc["discharge_threshold"] = dischargingCurrentThreshold;
  doc["soc"] = soc;
  doc["current_deadzone"] = currentDeadzoneThreshold;


  String jsonStr;
  serializeJson(doc, jsonStr);
  server.send(200, "application/json", jsonStr);
}


void handleSettingsPost() {
  if (!server.hasArg("plain")) {
    server.send(400, "text/plain", "Body missing");
    return;
  }

  // Debug log to see exactly what was sent
  addSerialLog("Incoming settings JSON: " + server.arg("plain"));

  StaticJsonDocument<512> doc;
  if (deserializeJson(doc, server.arg("plain"))) {
    server.send(400, "text/plain", "Invalid JSON");
    return;
  }

  if (doc.containsKey("current_deadzone")) {
    currentDeadzoneThreshold = doc["current_deadzone"].as<float>();
    writeFloat(ADDR_CURRENT_DEADZONE, currentDeadzoneThreshold);
    addSerialLog("💾 Writing to DS3231 EEPROM @ " + String(ADDR_CURRENT_DEADZONE) + " -> " + String(currentDeadzoneThreshold, 4));
}

  if (doc.containsKey("capacity_ah")) {
    batteryCapacityAh = doc["capacity_ah"].as<float>();
    writeFloat(ADDR_BATTERY_CAPACITY, batteryCapacityAh);
  }
  if (doc.containsKey("voltage_offset")) {
    voltageOffset = doc["voltage_offset"].as<float>();
    writeFloat(ADDR_VOLTAGE_OFFSET, voltageOffset);
  }
  if (doc.containsKey("current_offset")) {
    currentOffset = doc["current_offset"].as<float>();
    writeFloat(ADDR_CURRENT_OFFSET, currentOffset);
  }
  if (doc.containsKey("mv_per_amp")) {
    mVperAmp = doc["mv_per_amp"].as<float>();
    writeFloat(ADDR_MV_PER_AMP, mVperAmp);
  }
  if (doc.containsKey("charge_threshold")) {
    chargingCurrentThreshold = doc["charge_threshold"].as<float>();
    writeFloat(ADDR_CHARGING_THRESHOLD, chargingCurrentThreshold);
  }
  if (doc.containsKey("discharge_threshold")) {
    dischargingCurrentThreshold = doc["discharge_threshold"].as<float>();
    writeFloat(ADDR_DISCHARGING_THRESHOLD, dischargingCurrentThreshold);
  }

 // Always handle SOC, keeping old value if not sent
  if (!doc.containsKey("soc")) {
      doc["soc"] = soc; // keep old value
  }
  soc = doc["soc"].as<float>();
  if (soc < 0) soc = 0;
  if (soc > 100) soc = 100;
  totalCoulombs = (soc / 100.0) * batteryCapacityAh * 3600.0;
  writeFloat(ADDR_SOC, soc);
  addSerialLog("SOC updated via /settings API to " + String(soc) + "%");

  lastActiveStateChange = millis(); // ✅ reset idle timer so voltage SOC won't override immediately

  addSerialLog("Settings updated via API.");
  server.send(200, "text/plain", "Settings updated and saved to EEPROM.");
}

/*
  ==== MODIFIED handler: this will attempt to connect to provided SSID/PASS
       in AP+STA mode, wait up to a timeout for STA IP assignment, then
       respond with JSON that includes the assigned 'sta_ip'. After sending
       the response, it waits briefly and then restarts the ESP.
*/
void handleWiFiConfig() {
    if (!server.hasArg("plain")) {
        server.send(400, "text/plain", "Body missing");
        return;
    }

    StaticJsonDocument<256> req;
    if (deserializeJson(req, server.arg("plain"))) {
        server.send(400, "text/plain", "Invalid JSON");
        return;
    }

    const char* newSsid = req["ssid"];
    const char* newPass = req["password"];
    if (newSsid && newPass) {
        // Save credentials to EEPROM (or whatever storage)
        saveWiFiCredentials(newSsid, newPass);
        addSerialLog("WiFi config updated via API: SSID=" + String(newSsid));

        // Tell the client we started connecting (we will reply with final JSON below)
        addSerialLog("Attempting STA connect while keeping AP up (WIFI_AP_STA)");

        // Keep AP alive while trying to join the router
        WiFi.mode(WIFI_AP_STA);

        // Start the STA connection
        WiFi.begin(newSsid, newPass);

        // Try to connect for up to timeoutMs milliseconds
        const unsigned long timeoutMs = 20000; // 20 seconds (adjustable)
        unsigned long startAttempt = millis();
        bool connected = false;
        IPAddress assignedIp = IPAddress(0,0,0,0);

        while (millis() - startAttempt < timeoutMs) {
            // call handleClient so other routes remain responsive
            server.handleClient();

            if (WiFi.status() == WL_CONNECTED) {
                assignedIp = WiFi.localIP();
                connected = true;
                addSerialLog("STA connected — IP: " + assignedIp.toString());
                break;
            }
            delay(10); // shorter wait
            yield();   // allow WiFi + background tasks to run
        }

        if (!connected) {
            addSerialLog("STA did not connect within timeout.");
        }

        // Build JSON response containing the STA IP (or NOT_CONNECTED)
        StaticJsonDocument<256> resp;
        if (connected && assignedIp != IPAddress(0,0,0,0)) {
            resp["status"] = "OK";
            resp["sta_ip"] = assignedIp.toString();
        } else {
            resp["status"] = "NOT_CONNECTED";
            resp["sta_ip"] = "";
        }
        String out;
        serializeJson(resp, out);

        // Send JSON response back to app (app should read this before we reboot)
        server.send(200, "application/json", out);

        // Give the client a moment to receive the response (adjustable)
        delay(1200);

        // Optionally log and restart to ensure clean state (you may choose to skip reboot if connected)
        addSerialLog("Rebooting now to apply network changes.");
        delay(300); // tiny extra wait
        ESP.restart();
    } else {
        server.send(400, "text/plain", "Missing ssid or password");
    }
}


void handleSerialLog() {
  String logContent;
  int idx = logIndex;
  for (int i = 0; i < MAX_LOG_LINES; i++) {
    logContent += serialLogBuffer[idx] + "\n";
    idx = (idx + 1) % MAX_LOG_LINES;
  }
  server.send(200, "text/plain", logContent);
}

void handleNotFound() {
  server.send(404, "text/plain", "Not Found");
}

// ===================== HTML WiFi Config =====================

void handleWiFiConfigPage() {
  String html = R"rawliteral(
    <html>
    <head><title>WiFi Setup</title></head>
    <body style="font-family: Arial; text-align:center;">
      <h2>Configure WiFi</h2>
      <form onsubmit="sendData(event)">
        <label>SSID:</label><br>
        <input type="text" id="ssid" required><br><br>
        <label>Password:</label><br>
        <input type="password" id="password" required><br><br>
        <input type="submit" value="Save WiFi">
      </form>
      <p id="status"></p>
      <script>
        function sendData(e) {
          e.preventDefault();
          var ssid = document.getElementById('ssid').value;
          var pass = document.getElementById('password').value;
          fetch('/wifi_config', {
            method: 'POST',
            headers: {'Content-Type': 'application/json'},
            body: JSON.stringify({ssid: ssid, password: pass})
          }).then(r => r.json()).then(j => {
            // show friendly text for browser users; app will parse JSON too
            if (j.status === 'OK') {
              document.getElementById('status').innerText = 'Assigned IP: ' + j.sta_ip + '. Rebooting...';
            } else {
              document.getElementById('status').innerText = 'Not connected to router. Rebooting...';
            }
          }).catch(err => {
            document.getElementById('status').innerText = 'Error: ' + err;
          });
        }
      </script>
    </body>
    </html>
  )rawliteral";
  server.send(200, "text/html", html);
}

// ========================= Init ============================

void setupServerRoutes() {
  server.on("/", HTTP_GET, []() {
    server.send(200, "text/plain", "ESP Battery Monitor");
  });

  server.on("/sta_ip", HTTP_GET, []() {
    if (WiFi.status() == WL_CONNECTED) {
      server.send(200, "text/plain", WiFi.localIP().toString());
    } else {
      server.send(200, "text/plain", "NOT_CONNECTED");
    }
  });

  server.on("/reboot", HTTP_POST, []() {
    server.send(200, "text/plain", "Rebooting...");
    addSerialLog("Reboot command received via API.");
    delay(500);
    ESP.restart();
  });

  server.on("/live_data", HTTP_GET, handleLiveData);

  // ✅ Unified settings handling for SOC + all other settings
  server.on("/settings", HTTP_GET, handleSettingsGet);
  server.on("/settings", HTTP_POST, handleSettingsPost);

  server.on("/wifi_config", HTTP_GET, handleWiFiConfigPage);
  server.on("/wifi_config", HTTP_POST, handleWiFiConfig);

  server.on("/serial_log", HTTP_GET, []() {
    String logContent;
    int idx = logIndex;
    for (int i = 0; i < MAX_LOG_LINES; i++) {
      logContent += serialLogBuffer[idx] + "\n";
      idx = (idx + 1) % MAX_LOG_LINES;
    }
    server.send(200, "text/plain", logContent);
  });

  server.onNotFound(handleNotFound);
}

// =================== WiFi Credentials ======================

void loadWiFiCredentials() {
  readString(ADDR_WIFI_SSID, savedSsid, sizeof(savedSsid));
  readString(ADDR_WIFI_PASS, savedPass, sizeof(savedPass));
}

void saveWiFiCredentials(const char* ssid, const char* pass) {
  writeString(ADDR_WIFI_SSID, ssid);
  writeString(ADDR_WIFI_PASS, pass);
}

// ================== System Logging API =====================

void logSystemStatus() {
  unsigned long uptime = millis() / 1000;
  int rssi = WiFi.status() == WL_CONNECTED ? WiFi.RSSI() : -999;
  int freeHeap = ESP.getFreeHeap();

  addSerialLog("Uptime: " + String(uptime) + "s");
  addSerialLog("Free heap memory: " + String(freeHeap) + " bytes");

  if (rssi != -999) {
    addSerialLog("WiFi RSSI: " + String(rssi) + " dBm");
  }
}

void logSensorStatus() {
  String status = (filteredCurrent > chargingCurrentThreshold)
                      ? "Charging"
                      : ((filteredCurrent < -dischargingCurrentThreshold)
                             ? "Discharging"
                             : "Idle");

  String msg = "Voltage: " + String(currentVoltage) + " V, " +
               "Current: " + String(filteredCurrent) + " A, " +
               "Power: " + String(currentPower) + " W, " +
               "SOC: " + String(soc) + "%, Status: " + status;

  addSerialLog(msg);
}
