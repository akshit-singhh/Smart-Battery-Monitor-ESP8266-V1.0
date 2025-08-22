/*
   Project   : Smart Battery Monitor (ESP8266 V1.0)
   File      : AppServer.h
   Author    : Akshit Singh (github.com/akshit-singhh)
   License   : MIT License

   Description:
   Header file for AppServer.cpp.
   Declares functions and global objects related to web server, 
   WiFi credentials, and logging.

   Exposed Functions:
   - setupServerRoutes() / setupServerRoutes_AP()
   - handleServerClient()
   - loadWiFiCredentials() / saveWiFiCredentials()
   - syncTimeFromNTPtoRTC()
   - logSystemStatus(), logSensorStatus()
   - addSerialLog()

   Exposed Globals:
   - ESP8266WebServer server
   - Saved WiFi SSID and Password buffers
*/

#ifndef APP_SERVER_H
#define APP_SERVER_H

#include <ESP8266WebServer.h>

// Stored WiFi credentials (in AppServer.cpp)
extern char savedSsid[32];
extern char savedPass[64];

// Web server object (defined in AppServer.cpp)
extern ESP8266WebServer server;

// Route setup functions
void setupServerRoutes(); // normal STA-mode routes
void setupServerRoutes_AP(const String& apIP, const String& apSsid, const String& apPass); // AP-mode routes

// Main loop / helpers
void handleServerClient();
void loadWiFiCredentials();
void saveWiFiCredentials(const char* ssid, const char* pass);
void syncTimeFromNTPtoRTC();

// Logging / status
void logSystemStatus();
void logSensorStatus();
void addSerialLog(const String& message);

#endif // APP_SERVER_H
