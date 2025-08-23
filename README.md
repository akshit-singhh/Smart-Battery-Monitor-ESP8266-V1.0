# 🔋 Smart Battery Monitor (ESP8266 V1.0)

[![Platform](https://img.shields.io/badge/platform-ESP8266-blue.svg)](#)
[![Status](https://img.shields.io/badge/status-active-success.svg)](#)
[![UI](https://img.shields.io/badge/UI-OLED-lightgrey.svg)](#)
[![Blynk IoT](https://img.shields.io/badge/Blynk-IoT-green.svg)](https://blynk.io/)
[![License: MIT](https://img.shields.io/badge/license-MIT-green.svg)](LICENSE)

A **menu-driven smart battery monitoring system** built on **ESP8266**, featuring a 0.96" OLED display, real-time clock (DS3231) with NTP sync, and persistent configuration stored in external EEPROM (AT24C32).
The system continuously measures voltage, current, power, and state of charge (SOC) using sensors like INA219, WCS1600, and ADS1115, and logs data with uptime + timestamps.
It exposes a RESTful API for integration with a companion **Android app**, and is fully integrated with the **Blynk IoT platform** — providing both mobile app dashboards and a web-based interface for remote monitoring and control.
Additional features include QR code–based WiFi provisioning, on-device calibration menus, statistics tracking (Wh, cycles, uptime), and AP/STA dual WiFi modes.
Designed for expandability, with future upgrades planned for ESP32 hardware, MQTT integration (Home Assistant, Node-RED), and an advanced web dashboard with charts and controls.

For further info visit to- https://smartbatterymonitor.blogspot.com/2025/08/smart-battery-monitor-esp8266-diy.html

---

## ✨ Features
- 📟 **Menu-based OLED UI**
  - Navigate through menus to view Voltage, Current, Power, SOC, WiFi info, etc.
- 🌐 **WiFi provisioning**
  - Works in **AP mode** for setup
  - Configurable via `/wifi_config` (JSON API or HTML form)
  - QR code page (`/ap_qr`) for easy WiFi onboarding
- 🔌 **Telemetry REST API**
  - `/live_data` → real-time JSON with voltage, current, SOC, WiFi mode, RSSI, IP
  - `/serial_log` → rolling log buffer (~50 lines)
  - `/settings` → read/update calibration & SOC
  - `/sta_ip`, `/reboot`, etc.
- ⏰ **RTC with NTP sync**
  - DS3231 keeps accurate time, synced from NTP (IST, 12-hour with AM/PM)
  - Logs include uptime + real timestamps
- 💾 **EEPROM-backed persistence**
  - WiFi SSID/password
  - Calibration values (offsets, mV/Amp, thresholds)
  - Battery capacity, SOC, current deadzone
- 📲 **App integration ready**
  - Designed for Android app (Kotlin + Retrofit) to fetch `/live_data`, `/serial_log`, and push settings

---

## 🛠️ Hardware
- **ESP8266** (NodeMCU)
- **DS3231 RTC + AT24C32 EEPROM(already in DS3231)**
- **INA219** – Voltage & Current sensor
- **WCS1600** – Current sensor
- **0.96" OLED Display** (I2C)

---

## 📸 Screenshots

<p align="center">
  <img src="https://github.com/user-attachments/assets/6fd3c69e-330f-4085-b94f-b30eb7be8935" alt="PCB_PCB_Battery_level_indicator_2025-08-23 (2)" width="600" />
</p>

<p align="center">
  <img src="https://github.com/user-attachments/assets/b31cbb1c-5796-4507-a02d-184c19c7479d" alt="pcb" width="400" />
  <img src="https://github.com/user-attachments/assets/ea9f43d1-ca94-4546-8747-10a35f249eb8" alt="Screenshot 2025-08-23 172144" width="500" />
</p>


## 📟 OLED Menu System
- Navigation: Up / Down / Select / Back.
- Screen timeout: default 30 s (configurable).
## Main Menu
- Live Data View → real-time screen (Voltage, Current, Power, SOC, WiFi, uptime)
- Configuration
- Calibration
- Statistics
- System Info
- Github → shows QR code linking to your GitHub profile (github.com/akshit-singhh)
- Activate AP Mode → open AP control submenu
## Configuration
- Battery Settings
- Set screen timeout
## Battery Settings
- Set battery capacity (Ah)
- Set voltage thresholds (min/max)
- Select battery type → choices: Li-ion, Lead Acid, Li-Po
- Reset SOC to 100%
## Calibration
- Current Sensor Calibration
- Voltage Calibration
- Save/Load Calibration
## Current Sensor Calibration
- Auto-zero current sensor
- Manual zero offset
- Set Charge Curr
- Set Discharge Curr
- Set mV per Amp value
## Voltage Calibration
- Adjust voltage reading offset
- Calibrate with known voltage source
- Save/Load Calibration
- Save to EEPROM
- Load from EEPROM
- Reset to defaults
## Statistics
- Cycle Count
- Total Energy (Wh)
- Runtime History
- Reset Statistics
## System Info
- Firmware Version
- Sensor Status
- Memory Usage
- Uptime
- About
## AP Mode
- Start AP Mode
- Stop AP Mode

During AP setup, a guided screen shows:

   - AP Details (SSID/Password/IP)

   - QR Code to quickly open the WiFi setup URL

   - Skip setup

## 📦 Installation

1. Clone this repository:
   ```bash
   git clone https://github.com/akshit-singhh/Smart-Battery-Monitor-ESP8266-V1.0.git
2. Open BatteryMonitor.ino in Arduino IDE.
3. Select Board: NodeMCU 1.0 (ESP-12E Module)
4. Install required libraries:
ArduinoJson
RTClib
ESP8266 core libs (ESP8266WiFi, ESP8266WebServer)
5. Upload to your ESP8266.

## 📚 Required Libraries

Before compiling, make sure the following libraries are installed in your Arduino IDE:

1. **ESP8266 Core for Arduino**  
   - Provides `ESP8266WiFi.h`, `ESP8266WebServer.h`, `ESP.getFreeHeap()` etc.  
   - Install via **Boards Manager**:  
     - Arduino IDE → Tools → Board → Boards Manager → search **ESP8266 by ESP8266 Community**

2. **ArduinoJson** (by Benoît Blanchon)  
   - Used for building and parsing JSON in REST API routes.  
   - Install via **Library Manager** → search **ArduinoJson**.

3. **RTClib** (by Adafruit)  
   - Required for DS3231 RTC (`RTC_DS3231 rtc;`).  
   - Install via **Library Manager** → search **RTClib**.

4. **Adafruit INA219**  
   - For battery voltage and current measurements.  
   - Install via **Library Manager** → search **Adafruit INA219**.

5. **Adafruit SSD1306**  
   - For the 0.96" OLED display.  
   - Also installs **Adafruit GFX** automatically.  
   - Install via **Library Manager** → search **Adafruit SSD1306**.

6. **Wire** (I²C)  
   - Used for DS3231 RTC and AT24C32 EEPROM.  
   - Already included with Arduino IDE (no manual install needed).
7. **Adafruit ADS1X15**
   - For ADS1015/ADS1115 external ADC.
   - Install via Library Manager → search Adafruit ADS1X15.
8. **QRCode**
   - For generating QR codes (qrcode.h).
   - Install from GitHub [ricmoo/QRCode](https://github.com/ricmoo/QRCode) or Library Manager (search QRCode).
   
📌 **Tip:**  
All of the above can be installed easily via:  

Arduino IDE → Sketch → Include Library → Manage Libraries

---

## 📡 REST API Reference

## GET /live_data
Returns live telemetry.
```bash
{
  "voltage": 12.34,
  "current": 1.23,
  "soc": 87.5,
  "power": 15.18,
  "status": "Charging|Discharging|Idle",
  "rssi": -60,
  "mode": "AP|STA|AP_STA|NONE",
  "ip": "192.168.x.x"
}
```
## GET /serial_log
Returns the last ~50 log lines with uptime + timestamp.

## GET /settings
```bash
{
  "capacity_ah": 100.0,
  "voltage_offset": 0.0,
  "current_offset": 0.0,
  "mv_per_amp": 100.0,
  "charge_threshold": 0.5,
  "discharge_threshold": 0.5,
  "soc": 75.0,
  "current_deadzone": 0.05
}
```
## POST /settings
Update configuration (values saved to EEPROM).
```bash
{
  "soc": 80.0,
  "voltage_offset": 0.1,
  "current_deadzone": 0.03
}
```

POST /wifi_config
Send WiFi credentials:
```bash
{
  "ssid": "MyWiFi",
  "password": "12345678"
}
```
- Responds with assigned STA IP (if connected).
- Device reboots after applying.

## Other Endpoints

- GET /sta_ip → Returns STA IP or "NOT_CONNECTED"

- POST /reboot → Restarts device

- GET /ap_qr → QR code for WiFi setup page

- GET /ap_details → Shows SSID, Password, IP in AP mode

## 🚀 Future Roadmap

- ESP32 Version 2.0 (more resources & features)

- On-device web dashboard (graphs & controls)

- MQTT integration (Home Assistant / Node-RED)

- Advanced analytics (Wh/Ah counters, history export)

## 🙌 Author
Developed by Akshit Singh

GitHub: [@akshit-singhh](https://github.com/akshit-singhh)

## 🙋‍♂️ Contact
Made with ❤ by Akshit Singh

📧 Email: akshitsingh658@gmail.com

🔗 LinkedIn: linkedin.com/in/akshit-singhh

## ⭐ Support
If you found this project useful, don’t forget to ⭐ the repository!

