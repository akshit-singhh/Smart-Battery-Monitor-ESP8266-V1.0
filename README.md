# 🔋 Smart Battery Monitor (ESP8266 V1.0)

[![Platform](https://img.shields.io/badge/platform-ESP8266-blue.svg)](#)
[![License: MIT](https://img.shields.io/badge/license-MIT-green.svg)](LICENSE)
[![Status](https://img.shields.io/badge/status-active-success.svg)](#)
[![UI](https://img.shields.io/badge/UI-OLED-lightgrey.svg)](#)

A **menu-driven battery monitoring system** built on **ESP8266**, with OLED display, persistent settings in EEPROM, RTC timekeeping, and a RESTful API for integration with a companion Android app.

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
- **ESP8266** (NodeMCU / Wemos D1 Mini)
- **DS3231 RTC + AT24C32 EEPROM**
- **INA219** – Voltage & Current sensor
- **WCS1600** – Current sensor
- **0.96" OLED Display** (I2C)

---

## 📦 Installation

1. Clone this repository:
   ```bash
   git clone https://github.com/akshit-singhh/Smart-Battery-Monitor-ESP8266-V1.0.git
