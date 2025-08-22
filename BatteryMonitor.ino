/*
   Battery Monitor (ESP8266) - Version 1.0
   ---------------------------------------
   Author   : Your Name (github.com/akshit-singhhh)
   Date     : August 2025
   Hardware : ESP8266 (NodeMCU), INA219, WCS1600, DS3231 RTC, OLED
   License  : MIT License

   Description:
   This firmware monitors a battery system using voltage/current sensors
   and provides real-time data via Wi-Fi. The ESP8266 acts as a web server
   exposing REST APIs for live data, logging, and device configuration.

   Features:
   - Live telemetry: voltage, current, power, SOC
   - OLED display for local readout
   - Wi-Fi AP + STA provisioning
   - RTC (DS3231) timekeeping with NTP sync
   - REST endpoints for app integration:
       • /live_data   - JSON live values
       • /serial_log  - recent logs
       • /settings    - get/set config + SOC
       • /wifi_config - Wi-Fi credentials
       • /reboot      - restart device

   Notes:
   - All calibration & SOC values are stored in external EEPROM (AT24C32).
   - This version is designed for ESP8266; future ESP32 version will extend functionality.
*/

#include <Wire.h>
#include <Adafruit_ADS1X15.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_INA219.h>
#include <RTClib.h>
#include <Arduino.h> // For constrain() and other core functions
#include <stdio.h>   // For sprintf()
#include <ESP.h>     // For ESP.getFreeHeap()
#include "qrcode.h"
#include "EEPROMUtils.h"
#include "AppServer.h"
#include <ESP8266HTTPClient.h> 

#define BLYNK_TEMPLATE_ID "" //Your Template ID
#define BLYNK_TEMPLATE_NAME "Smart Battery Monitor"
#define BLYNK_AUTH_TOKEN "" //Your Auth Token

#include <ESP8266WiFi.h>
#include <BlynkSimpleEsp8266.h>
#include <Blynk.h>

#define AP_SSID "BatteryMonitor-Setup"
#define AP_PASS "12345678"

// ======================= OLED Display =======================
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

// ==== Function Prototypes ====
void startAPMode();
void showAPMenuOLED();
void displayAPDetails(const String& apIP);
void displayAPQRCode(const String& apIP);


// ======================= Sensors =======================
Adafruit_INA219 ina219;
Adafruit_ADS1115 ads;
RTC_DS3231 rtc;

// ======================= EEPROM (AT24C32 on DS3231 module) =======================
const uint8_t EEPROM_ADDR = 0x57;
const uint16_t ADDR_ZERO_ADC = 0;
const uint16_t ADDR_COULOMBS = 10;
const uint16_t ADDR_BATTERY_CAPACITY = 20;
const uint16_t ADDR_VOLTAGE_OFFSET = 30;
const uint16_t ADDR_CURRENT_OFFSET = 40;
const uint16_t ADDR_MV_PER_AMP = 50;
const uint16_t ADDR_VOLTAGE_THRESHOLD_MIN = 60;
const uint16_t ADDR_VOLTAGE_THRESHOLD_MAX = 70;
const uint16_t ADDR_BATTERY_TYPE = 80;
const uint16_t ADDR_SCREEN_TIMEOUT = 90;
const uint16_t ADDR_STATS_CYCLE_COUNT = 100;
const uint16_t ADDR_STATS_TOTAL_ENERGY_IN = 110;
const uint16_t ADDR_STATS_TOTAL_ENERGY_OUT = 120;
const uint16_t ADDR_CALIBRATION_SAVED = 130;
const uint16_t ADDR_SOC = 140;
const uint16_t ADDR_SOC_SAVED_FLAG = 150;
const uint16_t ADDR_CHARGING_SECONDS = 160;
const uint16_t ADDR_DISCHARGING_SECONDS = 170;
const uint16_t ADDR_IDLE_SECONDS = 180;
const uint16_t ADDR_CHARGING_THRESHOLD = 200;
const uint16_t ADDR_DISCHARGING_THRESHOLD = 210;
const uint16_t ADDR_CURRENT_DEADZONE = 220;

// --- WiFi first-boot skip flag (stored as float 0.0/1.0 to reuse existing helpers)
const uint16_t ADDR_WIFI_SKIP_F = 230;   // free slot; not used by current addresses

//Runtime
const uint16_t EVENT_LOG_START_ADDR = 300;
const uint8_t MAX_LOGS = 10;
const uint8_t LOG_ENTRY_SIZE = 5;
uint8_t eventLogIndex = 0;
int logViewOffset = 0;

enum EventType {
    EVENT_SOC_FULL = 1,
    EVENT_SOC_LOW = 2,
    EVENT_VOLTAGE_HIGH = 3,
    EVENT_VOLTAGE_LOW = 4,
    EVENT_START_CHARGING = 5,
    EVENT_START_DISCHARGING = 6,
    EVENT_IDLE = 7
};

// ======================= Menu State Variables =======================
enum MenuState {
	STATE_MAIN_DISPLAY,
	STATE_MAIN_MENU,
	STATE_CONFIG_MENU,
	STATE_CALIBRATION_MENU,
	STATE_STATS_MENU,
	STATE_SYSTEM_INFO_MENU,
	STATE_VIEW_QR_CODE,
  STATE_AP_MODE_MENU,
	
	// Configuration Submenu States
	STATE_BATTERY_SETTINGS_MENU,
	STATE_SET_CAPACITY,
	STATE_SET_VOLTAGE_THRESHOLDS_MIN,
	STATE_SET_VOLTAGE_THRESHOLDS_MAX,
	STATE_SET_BATTERY_TYPE,
	STATE_SET_SCREEN_TIMEOUT,
	STATE_RESET_SOC,

	// Calibration Submenu States
	STATE_CURRENT_CAL_MENU,
	STATE_VOLTAGE_CAL_MENU,
	STATE_SAVE_LOAD_CAL_MENU,
	STATE_AUTO_ZERO_CURRENT,
	STATE_MANUAL_ZERO_OFFSET,
	STATE_SET_MV_PER_AMP,
	STATE_ADJUST_VOLTAGE_OFFSET,
	STATE_CALIBRATE_KNOWN_VOLTAGE,
	STATE_SAVE_CALIBRATION,
	STATE_LOAD_CALIBRATION,
	STATE_RESET_CALIBRATION,
	STATE_SET_CHARGING_THRESHOLD,
	STATE_SET_DISCHARGING_THRESHOLD,
	
	// Statistics Submenu States
	STATE_VIEW_CYCLE_COUNT,
	STATE_VIEW_TOTAL_ENERGY,
	STATE_VIEW_RUNTIME_HISTORY,
	STATE_RESET_STATS,
	
	// System Info Submenu States
	STATE_VIEW_FIRMWARE,
	STATE_VIEW_SENSOR_STATUS,
	STATE_VIEW_MEMORY_USAGE,
	STATE_VIEW_UPTIME,
	STATE_ABOUT_MENU,

	// General States
	STATE_ACTION_IN_PROGRESS,
	STATE_MESSAGE,
	STATE_SCREEN_SAVER
	
};

// --- MENU STATE STACK ---
struct MenuHistory {
	MenuState state;
	int selectedIndex;
};
#define MAX_HISTORY_DEPTH 10
MenuHistory menuHistory[MAX_HISTORY_DEPTH];
int historyIndex = -1;

void logEvent(EventType type) {
    DateTime now = rtc.now();
    uint32_t timestamp = now.unixtime();
    uint16_t addr = EVENT_LOG_START_ADDR + (eventLogIndex % MAX_LOGS) * LOG_ENTRY_SIZE;
    Wire.beginTransmission(EEPROM_ADDR);
    Wire.write((uint8_t)(addr >> 8));
    Wire.write((uint8_t)(addr & 0xFF));
    Wire.write((uint8_t)type);
    Wire.write((uint8_t)(timestamp >> 24));
    Wire.write((uint8_t)(timestamp >> 16));
    Wire.write((uint8_t)(timestamp >> 8));
    Wire.write((uint8_t)(timestamp));
    Wire.endTransmission();
    delay(10);

    eventLogIndex++;
    if (eventLogIndex >= MAX_LOGS) eventLogIndex = 0;
}

void pushHistory(MenuState state, int selectedIndex) {
	if (historyIndex < MAX_HISTORY_DEPTH - 1) {
		historyIndex++;
		menuHistory[historyIndex].state = state;
		menuHistory[historyIndex].selectedIndex = selectedIndex;
	}
}

MenuHistory popHistory() {
	if (historyIndex >= 0) {
		return menuHistory[historyIndex--];
	}
	return {STATE_MAIN_DISPLAY, 0};
}

MenuState currentMenuState = STATE_MAIN_DISPLAY;
int selectedMenuIndex = 0;
int menuScrollOffset = 0;
const int visibleMenuItems = 4;
float tempFloatValue = 0.0;
int tempIntValue = 0;
const char* tempMessage = "";
unsigned long lastButtonPressTime = 0;
const unsigned long debounceDelay = 200;
int saverX = 0, saverY = 0;
int saverDX = 1, saverDY = 1;

// === CONFIGURABLE & VARIABLE ===
const char* FIRMWARE_VERSION = "1.2.3";
float batteryCapacityAh = 7.0;
float voltageOffset = 0.0;
float currentOffset = 0.0;
float mVperAmp = 22;
float minVoltageThreshold = 10.0;
float maxVoltageThreshold = 14.0;
uint8_t batteryType = 0;
unsigned int screenTimeout = 30;
float currentDeadzone = 0.25; // Default in Amps to set low current values 

// Statistics variables
uint32_t cycleCount = 0;
float totalEnergyInWh = 0.0;
float totalEnergyOutWh = 0.0;
// Runtime history variables
uint32_t chargingSeconds = 0;
uint32_t dischargingSeconds = 0;
uint32_t idleSeconds = 0;
unsigned long lastStateChangeMillis = 0;
unsigned long lastRuntimeSaveMillis = 0;
// === Constants ===
const float MV_PER_COUNT = 0.125;
float chargingCurrentThreshold = 0.6;
float dischargingCurrentThreshold = 1.0;

// === Button Pins ===
#define BACK_BUTTON_PIN D4 // GPIO2
#define SELECT_BUTTON_PIN D5 // GPIO14
#define UP_BUTTON_PIN D6 // GPIO12
#define DOWN_BUTTON_PIN D7 // GPIO13

// === Variables ===
float ZERO_CURRENT_ADC = -1;
float totalCoulombs = batteryCapacityAh * 3600.0;
float soc = 100.0;
unsigned long lastUpdate = 0;
unsigned long lastActivityTime = 0;
unsigned long lastActiveStateChange = 0;
bool screenIsOn = true;
unsigned long uptimeSeconds = 0;
unsigned long uptimeMillis = 0;
// Button state variables
bool buttonUpPressed = false;
bool buttonDownPressed = false;
bool buttonSelectPressed = false;
bool buttonBackPressed = false;
// Sensor status variables
bool ina219_present = false;
bool ads1115_present = false;
bool rtc_present = false;
char displayBuffer[30];
// Flag to track the first idle state
bool isFirstIdleStateReached = false;

// === Global variables for non-blocking sensor updates ===
BlynkTimer timer;

int lastRecordedDay = -1;  // To detect day change
int menuIndex = 0; // 0 = AP Details, 1 = QR Code
bool isSensorStable = false;
bool wifiSetupSkipped = false;
float backupTimeMinutes = 0.0;
float currentVoltage = 0.0;
float currentCurrent = 0.0;
float filteredCurrent = 0.0;
float currentPower = 0.0;
float currentDeadzoneThreshold = 0.25; // Default to 0.25 A
float getSocFromVoltage(float voltage);
unsigned long lastSensorUpdate = 0;
const unsigned long sensorUpdateInterval = 100;
static float filteredADC = 0.0;
static float filteredVoltage = 0.0;
static float filteredPower = 0.0;
unsigned long messageDisplayStartTime = 0;
const unsigned long messageDuration = 2000;

//internet checker
bool internetConnected = false;         // Global flag
unsigned long lastInternetCheck = 0;
const unsigned long INTERNET_CHECK_INTERVAL = 15UL * 60UL * 1000UL;  // 15 min

// ======================= Menu Item Arrays =======================
const char* mainMenuOptions[] = {
	"Live Data View",
	"Configuration",
	"Calibration",
	"Statistics",
	"System Info",
	"Github",
  "Activate AP Mode"
};
const int numMainMenuItems = sizeof(mainMenuOptions) / sizeof(mainMenuOptions[0]);

const char* configMenuOptions[] = {
	"Battery Settings",
	"Set screen timeout",
	"Back"
};
const int numConfigMenuItems = sizeof(configMenuOptions) / sizeof(configMenuOptions[0]);

const char* batterySettingsOptions[] = {
	"Set battery capacity (Ah)",
	"Set voltage thresholds (min/max)",
	"Select battery type",
	"Reset SOC to 100%",
	"Back"
};
const int numBatterySettingsItems = sizeof(batterySettingsOptions) / sizeof(batterySettingsOptions[0]);
const char* batteryTypes[] = {
	"Li-ion",
	"Lead Acid",
	"Li-Po"
};
const char* calibrationMenuOptions[] = {
	"Current Sensor Calibration",
	"Voltage Calibration",
	"Save/Load Calibration",
	"Back"
};
const int numCalibrationMenuItems = sizeof(calibrationMenuOptions) / sizeof(calibrationMenuOptions[0]);

const char* currentSensorCalOptions[] = {
	"Auto-zero current sensor",
	"Manual zero offset",
	"Set Charge Curr",
  "Set Discharge Curr",
	"Set mV per Amp value",
	"Back"
};
const int numCurrentSensorCalItems = sizeof(currentSensorCalOptions) / sizeof(currentSensorCalOptions[0]);

const char* voltageCalOptions[] = {
	"Adjust voltage reading offset",
	"Calibrate with known voltage source",
	"Back"
};
const int numVoltageCalItems = sizeof(voltageCalOptions) / sizeof(voltageCalOptions[0]);

const char* saveLoadCalOptions[] = {
	"Save to EEPROM",
	"Load from EEPROM",
	"Reset to defaults",
	"Back"
};
const int numSaveLoadCalItems = sizeof(saveLoadCalOptions) / sizeof(saveLoadCalOptions[0]);

const char* statsMenuOptions[] = {
	"Cycle Count",
	"Total Energy (Wh)",
	"Runtime History",
	"Reset Statistics",
	"Back"
};

// Manual AP Mode submenu
int apModeMenuIndex = 0;
const char* apModeMenuOptions[] = {
  "Start AP Mode",
  "Stop AP Mode",
  "Back"
};
const int apModeMenuCount = sizeof(apModeMenuOptions) / sizeof(apModeMenuOptions[0]);

const int numStatsMenuItems = sizeof(statsMenuOptions) / sizeof(statsMenuOptions[0]);

const char* systemInfoOptions[] = {
	"Firmware Version",
	"Sensor Status",
	"Memory Usage",
	"Uptime",
	"About",
	"Back"
};
const int numSystemInfoItems = sizeof(systemInfoOptions) / sizeof(systemInfoOptions[0]);

// ======================= Update all sensor data non-blocking =======================
// ==== WCS1600 Config (same as standalone code) ====
#define SENSOR_CHANNEL 0
#define CORRECTION_VALUE_mA 164
#define MEASUREMENT_ITERATIONS 100
#define WCS1600_SENSITIVITY_mV_PER_A 22.0

float zeroOffset_mV = 2600.0; // Will be recalibrated in setup()

// Persistent variables for non-blocking ADC averaging
static uint16_t adcSampleCount = 0;
static float adcSampleSum = 0;
static bool adcReady = false;

static unsigned long belowThresholdStart = 0;
const unsigned long DEAD_ZONE_HOLD_MS = 2000; // 2 seconds stable before clamping
const unsigned long IDLE_SOC_CORRECT_MS = 30UL * 60UL * 1000UL; // 30 minutes

//Soc voltage table
const float socLookupTable[][2] = {
    {12.7, 100.0},
    {12.5, 90.0},
    {12.4, 80.0},
    {12.3, 70.0},
    {12.2, 60.0},
    {12.0, 50.0},
    {11.9, 40.0},
    {11.8, 30.0},
    {11.6, 20.0},
    {11.5, 10.0},
    {11.4, 0.0}
};
const int socTableSize = sizeof(socLookupTable) / sizeof(socLookupTable[0]);

float getSocFromVoltage(float voltage) {
    if (voltage >= socLookupTable[0][0]) return socLookupTable[0][1];
    if (voltage <= socLookupTable[socTableSize - 1][0]) return socLookupTable[socTableSize - 1][1];

    for (int i = 0; i < socTableSize - 1; i++) {
        if (voltage <= socLookupTable[i][0] && voltage >= socLookupTable[i + 1][0]) {
            float v1 = socLookupTable[i][0];
            float soc1 = socLookupTable[i][1];
            float v2 = socLookupTable[i + 1][0];
            float soc2 = socLookupTable[i + 1][1];
            return soc2 + (soc1 - soc2) * ((voltage - v2) / (v1 - v2));
        }
    }
    return 0.0;
}

void updateSensors() {
    unsigned long now = millis();

    // --- Non-blocking ADC sample collection ---
    if (!adcReady) {
        int16_t adcReading = ads.readADC_SingleEnded(SENSOR_CHANNEL);
        float mV = ads.computeVolts(adcReading) * 1000.0; // Convert to mV
        adcSampleSum += mV;
        adcSampleCount++;

        yield();

        if (adcSampleCount >= MEASUREMENT_ITERATIONS) {
            float sensor_mV = adcSampleSum / (float)adcSampleCount;
            adcSampleSum = 0;
            adcSampleCount = 0;
            adcReady = true;

            // --- Apply WCS1600 accurate math ---
            float diff_mV = sensor_mV - zeroOffset_mV;
            currentCurrent = (diff_mV / WCS1600_SENSITIVITY_mV_PER_A) +
                            (CORRECTION_VALUE_mA / 1000.0);

            // Dead zone for both charging (+) and discharging (-)
            if (currentCurrent > -currentDeadzoneThreshold && currentCurrent < currentDeadzoneThreshold) {
                currentCurrent = 0.0;
            }
            filteredCurrent = currentCurrent;
        }
        return; // Wait for enough samples before continuing
    }

    if (now - lastSensorUpdate > sensorUpdateInterval) {
        adcReady = false; // Restart sampling for next cycle

        // --- Voltage measurement ---
        float voltageAlpha = 0.3;
        float rawVoltage = ina219.getBusVoltage_V();
        filteredVoltage = voltageAlpha * rawVoltage + (1.0 - voltageAlpha) * filteredVoltage;
        currentVoltage = filteredVoltage + voltageOffset;

        // --- Power calculation ---
        float rawPower = currentVoltage * currentCurrent;
        float powerAlpha = 0.25;
        filteredPower = powerAlpha * rawPower + (1.0 - powerAlpha) * filteredPower;
        currentPower = currentVoltage * currentCurrent;
        lastSensorUpdate = now;
    }

    // --- SOC and energy tracking logic ---
    float dt = (now - lastUpdate) / 1000.0;
    lastUpdate = now;
    float maxC = batteryCapacityAh * 3600.0;

    // Static variables for idle-based SOC correction
    static unsigned long lastNonIdleTime = 0;
    static bool idleSOCUsed = false;

    if (isFirstIdleStateReached) {
        if (filteredCurrent > chargingCurrentThreshold) {
            totalEnergyInWh += currentVoltage * filteredCurrent * dt / 3600.0;
            totalCoulombs = constrain(totalCoulombs + filteredCurrent * dt, 0, maxC);
            soc = (totalCoulombs / maxC) * 100.0;
            lastActiveStateChange = now;
            lastNonIdleTime = now; // activity detected
            idleSOCUsed = false;
        } 
        else if (filteredCurrent < -dischargingCurrentThreshold) {
            totalEnergyOutWh += currentVoltage * abs(filteredCurrent) * dt / 3600.0;
            totalCoulombs = constrain(totalCoulombs + filteredCurrent * dt, 0, maxC);
            soc = (totalCoulombs / maxC) * 100.0;
            lastActiveStateChange = now;
            lastNonIdleTime = now; // activity detected
            idleSOCUsed = false;
        } else {
            // Status is Idle → check if we've been idle long enough
            if (!idleSOCUsed && (now - lastNonIdleTime) >= IDLE_SOC_CORRECT_MS) {
              float oldSOC = soc; // Save current SOC before recalibration

        float newSOC = getSocFromVoltage(currentVoltage);
        if (newSOC < 0) newSOC = 0;
        if (newSOC > 100) newSOC = 100;

        soc = newSOC;
        totalCoulombs = (soc / 100.0) * maxC;
        writeFloat(ADDR_SOC, soc);

        // Highlighted log with icon + old→new SOC
        addSerialLog("⚡ [Hybrid SOC] Recalibration after idle: " 
                      + String(oldSOC, 2) + "% → " 
                      + String(newSOC, 2) + "%  (V=" + String(currentVoltage, 3) + ")");
        idleSOCUsed = true;
        }
      }
    } 
    else {
        if (filteredCurrent > -dischargingCurrentThreshold &&
            filteredCurrent < chargingCurrentThreshold) {
            isFirstIdleStateReached = true;
            totalCoulombs = (soc / 100.0) * maxC;
            lastNonIdleTime = now;
        }
    }
}

char lastValidBackupTimeStr[6] = "--:--";  // or "00:00" initially
void updateBlynkBackupTime() {
  float remainingCapacityAh = totalCoulombs / 3600.0;

  if (filteredCurrent < -dischargingCurrentThreshold) {
    // Actively discharging → calculate live backup time
    float dischargingCurrent = abs(filteredCurrent);

    // Prevent divide by zero or very small
    if (dischargingCurrent < 0.05) dischargingCurrent = 0.05;

    float totalBackupMinutes = (remainingCapacityAh / dischargingCurrent) * 60.0;
    if (totalBackupMinutes > 5999) totalBackupMinutes = 5999;

    int hours = floor(totalBackupMinutes / 60);
    int minutes = floor(fmod(totalBackupMinutes, 60));

    sprintf(lastValidBackupTimeStr, "%02d:%02d", hours, minutes);
  }
  // Always send the last known valid backup time
  Blynk.virtualWrite(V8, lastValidBackupTimeStr);
}

void updateBlynkChargingTime() {
  // Check if the battery is charging based on dynamic threshold
  if (filteredCurrent > chargingCurrentThreshold) {
    // Calculate the total capacity in Ampere-seconds
    float totalCapacityAs = batteryCapacityAh * 3600.0;

    // Calculate the remaining capacity needed to be fully charged
    float remainingCapacityAs = totalCapacityAs - totalCoulombs;

    // Avoid division by 0
    float chargingCurrent = filteredCurrent;
    if (chargingCurrent <= 0.01) chargingCurrent = 0.01;

    // Calculate time to full in seconds
    float timeToFullSeconds = remainingCapacityAs / chargingCurrent;

    // Convert to minutes
    float totalMinutesToFull = timeToFullSeconds / 60.0;

    // Calculate hours and remaining minutes
    int hours = floor(totalMinutesToFull / 60);
    int minutes = floor(fmod(totalMinutesToFull, 60));

    // Create a formatted string "hh:mm"
    char chargingTimeStr[6];
    sprintf(chargingTimeStr, "%02d:%02d", hours, minutes);

    // Send to Blynk
    Blynk.virtualWrite(V9, chargingTimeStr);
  } else {
    // If not charging, show "00:00"
    Blynk.virtualWrite(V9, "00:00");
  }
}


// ======================= Format hh:mm:ss string =======================
String formatTime(unsigned long totalSeconds) {
    unsigned long seconds = totalSeconds % 60;
    unsigned long minutes = (totalSeconds / 60) % 60;
    unsigned long hours = (totalSeconds / 3600) % 24;
    unsigned long days = totalSeconds / 86400;

    char buf[30];
    sprintf(buf, "%02lud:%02luh:%02lum:%02lus", days, hours, minutes, seconds);
    return String(buf);
}

// ======================= Recalibration Functions =======================
void drawProgressBar(int progress) {
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0);
    display.println("Calibration in progress...");
    int barWidth = map(progress, 0, 100, 0, SCREEN_WIDTH - 4);
    display.drawRoundRect(0, 20, SCREEN_WIDTH - 1, 10, 2, SSD1306_WHITE);
    display.fillRoundRect(2, 22, barWidth, 6, 2, SSD1306_WHITE);
    display.setCursor(50, 40);
    display.print(progress);
    display.print("%");
    display.display();
}

void showAPRunningScreen(const String& apIP, const char* ssidLabel) {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println("AP Mode Active");
  display.println("----------------");
  display.print("SSID: "); display.println(ssidLabel);
  display.print("PASS: "); display.println(AP_PASS);
  display.print("IP:   "); display.println(apIP);
  display.println();
  display.println("Use app at:");
  display.println("http://<IP>/wifi_config");
  display.display();
}


void recalibrateZeroADC() {
	int samples = 20;
	float sum = 0;
    currentMenuState = STATE_ACTION_IN_PROGRESS;
	for (int i = 0; i < samples; i++) {
		int32_t adcSum = 0;
		const int tempSamples = 20;
		for (int j = 0; j < tempSamples; j++) {
			adcSum += ads.readADC_SingleEnded(0);
			delayMicroseconds(300);
		}
		float avgADC = adcSum / (float)tempSamples;
		sum += avgADC;
		delay(2);
        drawProgressBar(map(i, 0, samples - 1, 0, 100));
	}
	ZERO_CURRENT_ADC = sum / samples;
	writeFloat(ADDR_ZERO_ADC, ZERO_CURRENT_ADC);
    currentMenuState = STATE_MESSAGE;
    tempMessage = "Auto-Zero Complete.";
    messageDisplayStartTime = millis();
}

void calibrateVoltageWithKnownSource(float knownVoltage) {
	float measuredVoltage = ina219.getBusVoltage_V() + voltageOffset;
	voltageOffset += knownVoltage - measuredVoltage;
	writeFloat(ADDR_VOLTAGE_OFFSET, voltageOffset);
    currentMenuState = STATE_MESSAGE;
    tempMessage = "Voltage calibration updated.";
    messageDisplayStartTime = millis();
}

// ======================= Statistics Functions =======================
void resetStatistics() {
	cycleCount = 0;
	totalEnergyInWh = 0.0;
	totalEnergyOutWh = 0.0;
	writeInt(ADDR_STATS_CYCLE_COUNT, cycleCount);
	writeFloat(ADDR_STATS_TOTAL_ENERGY_IN, totalEnergyInWh);
	writeFloat(ADDR_STATS_TOTAL_ENERGY_OUT, totalEnergyOutWh);
    currentMenuState = STATE_MESSAGE;
    tempMessage = "Statistics reset to 0.";
    messageDisplayStartTime = millis();
}

// ======================= Save/Load/Reset Calibration =======================
void saveCalibration() {
	writeFloat(ADDR_ZERO_ADC, ZERO_CURRENT_ADC);
	writeFloat(ADDR_VOLTAGE_OFFSET, voltageOffset);
	writeFloat(ADDR_CURRENT_OFFSET, currentOffset);
	writeFloat(ADDR_MV_PER_AMP, mVperAmp);
	writeInt(ADDR_CALIBRATION_SAVED, 1);
	writeFloat(ADDR_CHARGING_THRESHOLD, chargingCurrentThreshold);
  writeFloat(ADDR_DISCHARGING_THRESHOLD, dischargingCurrentThreshold);
    currentMenuState = STATE_MESSAGE;
    tempMessage = "Calibration Saved.";
    messageDisplayStartTime = millis();
}

void loadCalibration() {
	readFloat(ADDR_ZERO_ADC, &ZERO_CURRENT_ADC);
	voltageOffset = readFloat(ADDR_VOLTAGE_OFFSET);
	currentOffset = readFloat(ADDR_CURRENT_OFFSET);
	mVperAmp = readFloat(ADDR_MV_PER_AMP);
    currentMenuState = STATE_MESSAGE;
    tempMessage = "Calibration Loaded.";
    messageDisplayStartTime = millis();
		chargingCurrentThreshold = readFloat(ADDR_CHARGING_THRESHOLD);
		dischargingCurrentThreshold = readFloat(ADDR_DISCHARGING_THRESHOLD);

		if (chargingCurrentThreshold <= 0.0 || chargingCurrentThreshold > 10.0)
			chargingCurrentThreshold = 0.6;

		if (dischargingCurrentThreshold <= 0.0 || dischargingCurrentThreshold > 10.0)
			dischargingCurrentThreshold = 1.0;
}

void resetCalibrationDefaults() {
  ZERO_CURRENT_ADC = -1;
  voltageOffset = 0.0;
  currentOffset = 0.0;
  mVperAmp = 22;

  // ✅ Reset charging/discharging thresholds to defaults
  chargingCurrentThreshold = 0.6;
  dischargingCurrentThreshold = 1.0;

  // ✅ Save them to EEPROM
  writeFloat(ADDR_CHARGING_THRESHOLD, chargingCurrentThreshold);
  writeFloat(ADDR_DISCHARGING_THRESHOLD, dischargingCurrentThreshold);

  writeInt(ADDR_CALIBRATION_SAVED, 0);
  currentMenuState = STATE_MESSAGE;
  tempMessage = "Calibration defaults restored.";
  messageDisplayStartTime = millis();
}

// ======================= Functions for drawing different screens =======================
void drawMainScreen() {
  // The rest of the function remains the same, but without the SOC calculation.
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.print("Voltage V: "); display.print(currentVoltage, 2); display.print(" V");
  display.setCursor(0, 12); display.print("Current I: "); display.print(filteredCurrent, 2); display.print(" A");
  display.setCursor(0, 24); display.print("SoC: ");
  display.print(soc, 1);
  display.print(" %");
  display.setCursor(0, 36); display.print("Power: "); display.print(currentPower, 2); display.print(" W");
  display.setCursor(0, 48); display.print("Status: ");
  if (filteredCurrent > chargingCurrentThreshold) {
  display.print("Charging");
	} else if (filteredCurrent < -dischargingCurrentThreshold) {
		display.print("Discharging");
	} else {
		display.print("Idle");
	}
  display.display();
}

void drawMenu(const char* title, const char** items, int numItems, int selected, int offset) {
	display.clearDisplay();
	display.setTextSize(1);
	display.setTextColor(SSD1306_WHITE);
	int16_t x1, y1;
	uint16_t w, h;
	display.getTextBounds(title, 0, 0, &x1, &y1, &w, &h);
	display.setCursor((SCREEN_WIDTH - w) / 2, 0);
	display.drawFastHLine(0, 12, SCREEN_WIDTH, SSD1306_WHITE);  // Draw line below title
	display.println(title);

	for (int i = 0; i < visibleMenuItems; i++) {
		int itemIndex = offset + i;
		if (itemIndex >= numItems) {
			continue;
		}

		int yPos = 16 + i * 12;
		
		bool isSelected = (itemIndex == selected);
		if (isSelected) {
  display.fillRoundRect(0, yPos - 1, SCREEN_WIDTH - 7, 11, 2, SSD1306_WHITE);
  display.setTextColor(SSD1306_BLACK);
		}else {
			display.setTextColor(SSD1306_WHITE);
		}
		display.setCursor(4, yPos);
		display.print(items[itemIndex]);
	}

	if (numItems > visibleMenuItems) {
		int scrollbarHeight = 40;
		int scrollbarX = SCREEN_WIDTH - 5;
		display.drawRect(scrollbarX, 16, 3, scrollbarHeight, SSD1306_WHITE);
		int thumbHeight = max(5, (int)round((float)visibleMenuItems / numItems * scrollbarHeight));
		int thumbY = 16 + (int)round((float)offset / (numItems - visibleMenuItems) * (scrollbarHeight - thumbHeight));
		display.fillRect(scrollbarX, thumbY, 3, thumbHeight, SSD1306_WHITE);
	}
	display.display();
}

void drawValueScreen(const char* title, float value, int precision, float step) {
	display.clearDisplay();
	display.setTextSize(1);
	display.setTextColor(SSD1306_WHITE);
	int16_t x1, y1;
	uint16_t w, h;
	display.getTextBounds(title, 0, 0, &x1, &y1, &w, &h);
	display.setCursor((SCREEN_WIDTH - w) / 2, 0);
	display.drawFastHLine(0, 12, SCREEN_WIDTH, SSD1306_WHITE);  // Draw line below title
	display.println(title);
	display.drawFastHLine(0, 12, SCREEN_WIDTH, SSD1306_WHITE);

	display.setTextSize(2);
	display.setCursor(0, 32);
	display.print(value, precision);

	display.setTextSize(1);
	display.setCursor(0, 50);
	display.print("Step: ");
	display.print(step, precision);
	display.setCursor(100, 50);
	display.print("OK");
	display.display();
}

void drawMessageScreen(const char* title, const char* message) {
	display.clearDisplay();
	display.setTextSize(1);
	display.setTextColor(SSD1306_WHITE);
	int16_t x1, y1;
	uint16_t w, h;
	display.getTextBounds(title, 0, 0, &x1, &y1, &w, &h);
	display.setCursor((SCREEN_WIDTH - w) / 2, 0);
	display.drawFastHLine(0, 12, SCREEN_WIDTH, SSD1306_WHITE);  // Draw line below title
	display.println(title);
	display.drawFastHLine(0, 12, SCREEN_WIDTH, SSD1306_WHITE);
	display.setCursor(0, 20);
	display.println(message);
	display.display();
}

void drawSystemInfoScreen() {
	display.clearDisplay();
	display.setTextSize(1);
	display.setTextColor(SSD1306_WHITE);
	display.setCursor(0, 0);
	display.println("System Info");
	display.drawFastHLine(0, 12, SCREEN_WIDTH, SSD1306_WHITE);
	display.setCursor(0, 16);
	display.print("Firmware: ");
	display.println(FIRMWARE_VERSION);

	display.setCursor(0, 28);
	display.print("INA219: ");
	display.println(ina219_present ? "OK" : "ERR");
	display.setCursor(0, 40);
	display.print("ADS1115: ");
	display.println(ads1115_present ? "OK" : "ERR");
	
	display.setCursor(0, 52);
	display.print("RTC: ");
	display.println(rtc_present ? "OK" : "ERR");

	display.display();
}

void drawMemoryUsageScreen() {
	display.clearDisplay();
	display.setTextSize(1);
	display.setTextColor(SSD1306_WHITE);
	display.setCursor(0, 0);
	display.println("Memory Usage");
	display.drawFastHLine(0, 12, SCREEN_WIDTH, SSD1306_WHITE);
	display.setCursor(0, 16);

	uint32_t freeHeap = ESP.getFreeHeap();
	if (freeHeap < 1024) {
		display.print("Free RAM: ");
		display.print(freeHeap);
		display.println(" B");
	} else if (freeHeap < 1024 * 1024) {
		display.print("Free RAM: ");
		display.print(freeHeap / 1024.0, 2);
		display.println(" KB");
	} else {
		display.print("Free RAM: ");
		display.print(freeHeap / 1024.0 / 1024.0, 2);
		display.println(" MB");
	}

	display.display();
}

void drawUptimeScreen() {
	display.clearDisplay();
	display.setTextSize(1);
	display.setTextColor(SSD1306_WHITE);
	display.setCursor(0, 0);
	display.println("Uptime");
	display.drawFastHLine(0, 12, SCREEN_WIDTH, SSD1306_WHITE);
	display.setCursor(0, 20);
	display.println(formatTime(uptimeSeconds));
	display.display();
}

void drawAboutScreen() {
  display.clearDisplay();

  // Title: About (centered at top)
  const char* heading = "About";
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  int16_t x1, y1;
  uint16_t w, h;
  display.getTextBounds(heading, 0, 0, &x1, &y1, &w, &h);
  display.setCursor((SCREEN_WIDTH - w) / 2, 0);
  display.println(heading);

  // Separator line below title
  display.drawFastHLine(0, 12, SCREEN_WIDTH, SSD1306_WHITE);

  // Line 1: "Smart Battery"
  display.setTextSize(1);
  display.getTextBounds("Smart Battery", 0, 0, &x1, &y1, &w, &h);
  display.setCursor((SCREEN_WIDTH - w) / 2, 24);
  display.println("Smart Battery");

  // Line 2: "Monitor"
  display.getTextBounds("Monitor", 0, 0, &x1, &y1, &w, &h);
  display.setCursor((SCREEN_WIDTH - w) / 2, 36);
  display.println("Monitor");

  // Line 3: "By Akshit Singh"
  display.getTextBounds("By Akshit Singh", 0, 0, &x1, &y1, &w, &h);
  display.setCursor((SCREEN_WIDTH - w) / 2, 52);
  display.println("By Akshit Singh");

  display.display();
}

void drawQRCodeScreen() {
  display.clearDisplay();

  const char* githubUrl = "github.com/akshit-singhh";

  QRCode qrcode;
  uint8_t qrcodeData[qrcode_getBufferSize(2)];
  qrcode_initText(&qrcode, qrcodeData, 2, ECC_MEDIUM, githubUrl);

  const int moduleSize = 2;
  const int qrPixelSize = qrcode.size * moduleSize;  // 25 * 2 = 50px
  const int offsetX = (SCREEN_WIDTH - qrPixelSize) / 2;
  const int offsetY = (SCREEN_HEIGHT - qrPixelSize) / 2;

  // Draw QR code once
  for (int y = 0; y < qrcode.size; y++) {
    for (int x = 0; x < qrcode.size; x++) {
      if (qrcode_getModule(&qrcode, x, y)) {
        display.fillRect(offsetX + x * moduleSize, offsetY + y * moduleSize,
                         moduleSize, moduleSize, SSD1306_WHITE);
      }
    }
  }

  display.display();

  // Wait up to 15 seconds or exit on BACK or SELECT button press
  // Wait until buttons are released BEFORE starting timeout
while (digitalRead(BACK_BUTTON_PIN) == LOW || digitalRead(SELECT_BUTTON_PIN) == LOW) {
  delay(10);  // Wait for user to let go
}

unsigned long startTime = millis();
while (millis() - startTime < 15000) {
  if (digitalRead(BACK_BUTTON_PIN) == LOW || digitalRead(SELECT_BUTTON_PIN) == LOW) {
    break;
  }
  delay(10);
}

  // Return to main menu after exit
  currentMenuState = STATE_MAIN_MENU;
}

void checkAndResetDailyEnergy() {
  DateTime now = rtc.now();
  int currentDay = now.day();

  if (lastRecordedDay == -1) {
    lastRecordedDay = currentDay;  // On first boot
  }

  if (currentDay != lastRecordedDay) {
    totalEnergyInWh = 0.0;
    totalEnergyOutWh = 0.0;
    lastRecordedDay = currentDay;

    Serial.println("✅ Energy stats reset for new day.");
  }
}

void drawRuntimeHistoryScreen() {
	display.clearDisplay();
	display.setTextSize(1);
	display.setTextColor(SSD1306_WHITE);
	display.setCursor(0, 0);
	display.println("Runtime History");
	display.drawFastHLine(0, 12, SCREEN_WIDTH, SSD1306_WHITE);
	for (int i = 0; i < 4; i++) {
		int logIndex = (eventLogIndex + MAX_LOGS - 1 - logViewOffset - i + MAX_LOGS) % MAX_LOGS;
		uint16_t addr = EVENT_LOG_START_ADDR + logIndex * LOG_ENTRY_SIZE;
		Wire.beginTransmission(EEPROM_ADDR);
		Wire.write((uint8_t)(addr >> 8));
		Wire.write((uint8_t)(addr & 0xFF));
		Wire.endTransmission();
		Wire.requestFrom(EEPROM_ADDR, LOG_ENTRY_SIZE);
		if (Wire.available() < 5) continue;
		uint8_t type = Wire.read();
		uint32_t timestamp = 0;
		for (int j = 0; j < 4; j++) {
			timestamp = (timestamp << 8) | Wire.read();
		}
		DateTime dt(timestamp);
		char timeStr[10];
		sprintf(timeStr, "[%02d:%02d]", dt.hour(), dt.minute());
		String msg;
		switch (type) {
			case EVENT_SOC_FULL: msg = "SOC: 100%"; break;
			case EVENT_SOC_LOW: msg = "SOC <=40%"; break;
			case EVENT_VOLTAGE_HIGH: msg = "Volt High"; break;
			case EVENT_VOLTAGE_LOW: msg = "Volt Low";
			break;
			case EVENT_START_CHARGING: msg = "Charging start"; break;
			case EVENT_START_DISCHARGING: msg = "Discharging start"; break;
			case EVENT_IDLE: msg = "Idle start"; break;
			default: msg = "Unknown"; break;
		}
		display.setCursor(0, 16 + i * 12);
		display.print(timeStr);
		display.print(" ");
		display.println(msg);
	}
	display.display();
}

void drawAPModeMenu() {
    drawMenu("AP Mode Menu", apModeMenuOptions, apModeMenuCount, selectedMenuIndex, menuScrollOffset);
}


void drawScreenSaver() {
	display.clearDisplay();
	display.drawCircle(saverX, saverY, 5, SSD1306_WHITE);
	display.display();
	saverX += saverDX;
	saverY += saverDY;
	if (saverX >= SCREEN_WIDTH - 5 || saverX <= 5) {
		saverDX *= -1;
	}
	if (saverY >= SCREEN_HEIGHT - 5 || saverY <= 5) {
		saverDY *= -1;
	}
}
//Boot ProgressBar
void showBootProgressBar() {
  unsigned long startMillis = millis();
  unsigned long duration = 2000;
  int step = 0;

  while (millis() - startMillis < duration) {
    int progress = map(millis() - startMillis, 0, duration, 0, 100);

    // Gradual background loading
    if (step == 0 && millis() - startMillis > 100) {
      readFloat(ADDR_BATTERY_CAPACITY, &batteryCapacityAh);
      step++;
    } else if (step == 1 && millis() - startMillis > 300) {
      minVoltageThreshold = readFloat(ADDR_VOLTAGE_THRESHOLD_MIN);
      maxVoltageThreshold = readFloat(ADDR_VOLTAGE_THRESHOLD_MAX);
      step++;
    } else if (step == 2 && millis() - startMillis > 500) {
      readInt(ADDR_SCREEN_TIMEOUT, &screenTimeout);
      readInt(ADDR_STATS_CYCLE_COUNT, &cycleCount);
      step++;
    } else if (step == 3 && millis() - startMillis > 800) {
      totalEnergyInWh = readFloat(ADDR_STATS_TOTAL_ENERGY_IN);
      totalEnergyOutWh = readFloat(ADDR_STATS_TOTAL_ENERGY_OUT);
      step++;
    } else if (step == 4 && millis() - startMillis > 1100) {
      chargingSeconds = readInt(ADDR_CHARGING_SECONDS);
      dischargingSeconds = readInt(ADDR_DISCHARGING_SECONDS);
      idleSeconds = readInt(ADDR_IDLE_SECONDS);
      step++;
    } else if (step == 2 && millis() - startMillis > 500) {
    readInt(ADDR_SCREEN_TIMEOUT, &screenTimeout);
    readInt(ADDR_STATS_CYCLE_COUNT, &cycleCount);
    readFloat(ADDR_CURRENT_DEADZONE, &currentDeadzoneThreshold); // ← new
    step++;
    }else if (step == 5 && millis() - startMillis > 1400) {
  if (readInt(ADDR_CALIBRATION_SAVED) == 1) {
    loadCalibration();
  }
  step++;
} else if (step == 6 && millis() - startMillis > 1700) {
  uint32_t socFlag;
readInt(ADDR_SOC_SAVED_FLAG, &socFlag);
if (socFlag == 1) {
    readFloat(ADDR_SOC, &soc);
    totalCoulombs = readFloat(ADDR_COULOMBS);  // ← Restores totalCoulombs too
  } else {
    soc = 100.0;
    totalCoulombs = batteryCapacityAh * 3600.0;
    writeFloat(ADDR_SOC, soc);
    writeFloat(ADDR_COULOMBS, totalCoulombs);  // ← Initializes if first boot
    writeInt(ADDR_SOC_SAVED_FLAG, 1);
  }
  step++;
}
    // Draw boot progress bar
    int barWidth = map(progress, 0, 100, 0, SCREEN_WIDTH - 4);
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(30, 0);
    display.println("Booting...");

    display.drawRoundRect(0, 20, SCREEN_WIDTH - 1, 10, 3, SSD1306_WHITE);
    display.fillRoundRect(2, 22, barWidth, 6, 3, SSD1306_WHITE);

    display.setCursor(50, 40);
    display.print(progress);
    display.print("%");
    display.display();

    delay(10);  // smooth animation
  }
}

void showWelcomeScreen() {
  display.clearDisplay();

  // Draw outer border
  display.drawRoundRect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, 4, SSD1306_WHITE);

  display.setTextColor(SSD1306_WHITE);

  // Line 1: "Welcome to"
  display.setTextSize(1);
  int16_t x1, y1;
  uint16_t w, h;
  display.getTextBounds("Welcome to", 0, 0, &x1, &y1, &w, &h);
  display.setCursor((SCREEN_WIDTH - w) / 2, 8);
  display.println("Welcome to");

  // Line 2: "Smart Battery"
  display.setTextSize(1);
  display.getTextBounds("Smart Battery", 0, 0, &x1, &y1, &w, &h);
  display.setCursor((SCREEN_WIDTH - w) / 2, 24);
  display.println("Smart Battery");

  // Line 3: "Monitor"
  display.getTextBounds("Monitor", 0, 0, &x1, &y1, &w, &h);
  display.setCursor((SCREEN_WIDTH - w) / 2, 36);
  display.println("Monitor");

  // Line 4: "By Akshit Singh"
  display.setTextSize(1);
  display.getTextBounds("By Akshit Singh", 0, 0, &x1, &y1, &w, &h);
  display.setCursor((SCREEN_WIDTH - w) / 2, 52);
  display.println("By Akshit Singh");

  display.display();
  delay(3000);
}

void saveSocToEEPROM() {
  writeFloat(ADDR_SOC, soc);
  writeFloat(ADDR_COULOMBS, totalCoulombs);  // ← Save together
  Serial.print("Saved SOC: ");
  Serial.print(soc);
  Serial.print(" | Coulombs: ");
  Serial.println(totalCoulombs);
}

void saveEnergyStatsToEEPROM() {
  writeFloat(ADDR_STATS_TOTAL_ENERGY_IN, totalEnergyInWh);
  writeFloat(ADDR_STATS_TOTAL_ENERGY_OUT, totalEnergyOutWh);
  Serial.println("Energy stats saved to EEPROM.");
}
String getTimeUntilMidnight() {
  DateTime now = rtc.now();
  int secondsLeft = (23 - now.hour()) * 3600 + (59 - now.minute()) * 60 + (60 - now.second());

  int hours = secondsLeft / 3600;
  int minutes = (secondsLeft % 3600) / 60;
  int seconds = secondsLeft % 60;

  char buffer[16];
  sprintf(buffer, "%02d:%02d:%02d", hours, minutes, seconds);
  return String(buffer);
}

void startAPMode() {
  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASS);
  String apIP = WiFi.softAPIP().toString();

  Serial.println("AP Mode started");
  Serial.print("SSID: "); Serial.println(AP_SSID);
  Serial.print("Password: "); Serial.println(AP_PASS);
  Serial.print("IP Address: "); Serial.println(apIP);

  addSerialLog("Access Point started. SSID: " + String(AP_SSID) +
               ", PASS: " + String(AP_PASS) +
               ", IP: " + apIP);

  // ✅ Make sure /wifi_config and other AP routes are available
  setupServerRoutes_AP(apIP, AP_SSID, AP_PASS);
  server.begin();

  unsigned long lastMenuUpdate = 0;
  int apMenuIndex = 0; // now 0=AP Details, 1=QR Code, 2=Skip setup
  const int apMenuCount = 3;

  while (true) {
    server.handleClient(); // keep serving /wifi_config

    if (millis() - lastMenuUpdate > 100) {
      // Show menu on OLED
      showAPMenuOLED(apMenuIndex, apIP); // update this to show 3 options
      lastMenuUpdate = millis();
    }

    // Navigate menu
    if (digitalRead(UP_BUTTON_PIN) == LOW) {
      apMenuIndex = (apMenuIndex + 1) % apMenuCount;
      delay(200);
    }

    // Select
    if (digitalRead(SELECT_BUTTON_PIN) == LOW) {
      if (apMenuIndex == 0) {
        displayAPDetails(apIP);
      } 
      else if (apMenuIndex == 1) {
        displayAPQRCode(apIP);  // QR points to /wifi_config
      } 
      else if (apMenuIndex == 2) {
        // User chose to skip setup
        wifiSetupSkipped = true; // set global flag
        WiFi.softAPdisconnect(true);
        WiFi.mode(WIFI_OFF);
        addSerialLog("User skipped WiFi setup. Running offline.");
        return; // Exit AP mode and return to normal run
      }
      delay(200);
    }

    yield();
  }
}

void showAPStatusScreen(const String &title, const String &ssid, const String &pass, const String &ip) {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println(title);
  display.println("----------------");
  display.print("SSID: ");
  display.println(ssid);
  display.print("PASS: ");
  display.println(pass);
  display.print("IP: ");
  display.println(ip);
  display.display();

  // Wait until user presses BACK
  while (true) {
    server.handleClient(); // Keep HTTP active
    if (digitalRead(BACK_BUTTON_PIN) == LOW) {
      while (digitalRead(BACK_BUTTON_PIN) == LOW) delay(10); // Wait for release
      break;
    }
    delay(1);
  }
}

// First-boot/setup AP (legacy, already used by startAPMode)
#define AP_SSID        "BatteryMonitor-Setup"   // keep existing
#define AP_PASS        "12345678"

// Manual AP (from main menu)
#define AP_SSID_MENU   "Battery Monitor AP"  // NEW: distinguish from first boot AP


void connectWiFi() {
  loadWiFiCredentials(); // Loads savedSsid and savedPass

  if (strlen(savedSsid) == 0 || strlen(savedPass) == 0) {
    if (wifiSetupSkipped) {
      Serial.println("WiFi setup skipped. Running offline.");
      addSerialLog("WiFi setup skipped. Running offline.");
      WiFi.mode(WIFI_OFF);
      internetConnected = false;
      return;
    } else {
      Serial.println("No WiFi credentials. Starting AP mode.");
      addSerialLog("No WiFi credentials. Starting AP mode.");
      startAPMode();
      internetConnected = false;
      return;
    }
  }

  Serial.println("Connecting to saved WiFi...");
  addSerialLog("Connecting to WiFi SSID: " + String(savedSsid));

  WiFi.mode(WIFI_STA);
  WiFi.begin(savedSsid, savedPass);

  unsigned long startAttemptTime = millis();
  int dotCount = 0;
  while (WiFi.status() != WL_CONNECTED && millis() - startAttemptTime < 10000) {
    yield();
    delay(100);
    Serial.print(".");
    if (++dotCount >= 50) {
      Serial.println();
      dotCount = 0;
    }
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("✅ WiFi connected!");
    Serial.print("IP: ");
    Serial.println(WiFi.localIP());

    if (isInternetAvailable()) {
      Serial.println("🌐 Internet is available!");
      addSerialLog("WiFi + Internet OK");
      internetConnected = true;

      setupServerRoutes();
      server.begin();

      Blynk.config(BLYNK_AUTH_TOKEN);
      Blynk.connect(5000);  // Non-blocking

      delay(3000);  // Allow internet/DNS to fully initialize
      checkAndFixRtcTime();  // Safe to run now

    } else {
      Serial.println("❌ WiFi OK, but no internet.");
      addSerialLog("WiFi connected, but internet not available.");
      internetConnected = false;

      setupServerRoutes();
      server.begin();
    }

  } else {
    Serial.println("❌ WiFi connection failed.");
    addSerialLog("WiFi connection failed. Starting AP mode.");
    internetConnected = false;
    startAPMode();
  }
}





// MENU display
void showAPMenuOLED(int apMenuIndex, const String &apIP) {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println("AP Mode - Setup");
  display.println();

  display.println(apMenuIndex == 0 ? "> AP Details" : "  AP Details");
  display.println(apMenuIndex == 1 ? "> QR Code" : "  QR Code");
  display.println(apMenuIndex == 2 ? "> Skip setup" : "  Skip setup");

  display.display();
}

void showAPModeSubmenu() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println("Activate AP Mode");
  display.println();

  for (int i = 0; i < apModeMenuCount; i++) {
    if (i == selectedMenuIndex) {
      display.print("> ");
    } else {
      display.print("  ");
    }
    display.println(apModeMenuOptions[i]);
  }
  display.display();
}


#include <qrcode.h>

void displayAPDetails(const String &apIP) {
  // --- Flush any previous button presses before showing ---
  while (digitalRead(BACK_BUTTON_PIN) == LOW || digitalRead(SELECT_BUTTON_PIN) == LOW) {
    delay(10);
  }

  // === Step 1: Draw AP details on OLED ===
  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.setTextColor(SSD1306_WHITE);
  display.println("AP Mode Details:");
  display.println("----------------");
  display.print("SSID: ");
  display.println(AP_SSID);
  display.print("PASS: ");
  display.println(AP_PASS);
  display.print("IP: ");
  display.println(apIP);
  display.display();

  // === Step 2: Wait for *new* button press while serving HTTP ===
  while (true) {
    server.handleClient(); // ✅ Keep Wi-Fi config page responsive

    if (digitalRead(BACK_BUTTON_PIN) == LOW || digitalRead(SELECT_BUTTON_PIN) == LOW) {
      // Wait for release before exiting
      while (digitalRead(BACK_BUTTON_PIN) == LOW || digitalRead(SELECT_BUTTON_PIN) == LOW) {
        delay(10);
      }
      break;
    }
    delay(1);
  }
}

void displayAPQRCode(const String &apIP) {
  // --- Flush any previous button presses before showing ---
  while (digitalRead(BACK_BUTTON_PIN) == LOW || digitalRead(SELECT_BUTTON_PIN) == LOW) {
    delay(10);
  }

  // === Step 1: Prepare QR data (point to WiFi config page)
  String qrData = "http://" + apIP + "/wifi_config";
  QRCode qrcode;
  uint8_t qrcodeData[qrcode_getBufferSize(3)];
  qrcode_initText(&qrcode, qrcodeData, 3, 0, qrData.c_str());

  // === Step 2: Draw QR code once
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);

  int scale = 2;
  int qrSize = qrcode.size * scale;
  int xOffset = (SCREEN_WIDTH - qrSize) / 2;
  int yOffset = (SCREEN_HEIGHT - qrSize) / 2;

  for (uint8_t y = 0; y < qrcode.size; y++) {
    for (uint8_t x = 0; x < qrcode.size; x++) {
      if (qrcode_getModule(&qrcode, x, y)) {
        display.fillRect(xOffset + x * scale, yOffset + y * scale, scale, scale, SSD1306_WHITE);
      }
    }
  }
  display.display();

  // === Step 3: Wait for *new* button press while serving HTTP
  while (true) {
    server.handleClient(); // ✅ Keep Wi-Fi config page responsive

    // Wait for user press after screen is shown
    if (digitalRead(BACK_BUTTON_PIN) == LOW || digitalRead(SELECT_BUTTON_PIN) == LOW) {
      // Wait for release before exiting
      while (digitalRead(BACK_BUTTON_PIN) == LOW || digitalRead(SELECT_BUTTON_PIN) == LOW) {
        delay(10);
      }
      break;
    }
    delay(1);
  }
}

void handleAPModeMenu() {
  unsigned long lastMenuUpdate = 0;
  bool inMenu = true;

  while (inMenu) {
    if (millis() - lastMenuUpdate > 100) {
      showAPModeSubmenu();
      lastMenuUpdate = millis();
    }

    if (digitalRead(UP_BUTTON_PIN) == LOW) {
      apModeMenuIndex = (apModeMenuIndex + 1) % apModeMenuCount;
      delay(200);
    }

    if (digitalRead(SELECT_BUTTON_PIN) == LOW) {
      if (apModeMenuIndex == 0) {
        activateAPModeFromMenu();
      }
      else if (apModeMenuIndex == 1) {
        deactivateAPModeFromMenu();
      }
      else if (apModeMenuIndex == 2) {
        inMenu = false; // Back
      }
      delay(200);
    }

    if (digitalRead(BACK_BUTTON_PIN) == LOW) {
      inMenu = false;
      delay(200);
    }
  }
}

//ntp code
bool isInternetAvailable() {
  if (WiFi.status() != WL_CONNECTED) return false;

  HTTPClient http;
  http.setTimeout(3000);
  http.begin("http://clients3.google.com/generate_204");
  int httpCode = http.GET();
  http.end();

  return (httpCode == 204);
}

bool isRtcTimeValid(time_t ntpEpoch, DateTime rtcTime, int maxAllowedDiffSec = 60) {
  // Convert rtcTime to epoch for comparison
  tm rtcTm = {};
  rtcTm.tm_year = rtcTime.year() - 1900;
  rtcTm.tm_mon = rtcTime.month() - 1;
  rtcTm.tm_mday = rtcTime.day();
  rtcTm.tm_hour = rtcTime.hour();
  rtcTm.tm_min = rtcTime.minute();
  rtcTm.tm_sec = rtcTime.second();
  time_t rtcEpoch = mktime(&rtcTm);

  long diff = abs((long)(ntpEpoch - rtcEpoch));
  return diff <= maxAllowedDiffSec; // valid if difference within maxAllowedDiffSec seconds
}

void checkAndFixRtcTime() {
  if (WiFi.status() != WL_CONNECTED) {
    addSerialLog("❌ WiFi not connected. Cannot check NTP.");
    return;
  }
  configTime(19800, 0,
    "0.in.pool.ntp.org",
    "1.in.pool.ntp.org",
    "2.in.pool.ntp.org"
  );

  Serial.print("Waiting for NTP time sync");
  time_t now = time(nullptr);
  int retries = 0;
  const int maxRetries = 30;
  while (now < 946684800 && retries < maxRetries) {
    waitMillis(300);
    Serial.print(".");
    now = time(nullptr);
    retries++;
  }
  Serial.println();

  if (now < 946684800) {
    addSerialLog("❌ NTP sync failed.");
    return;
  }
  struct tm timeinfo;
  localtime_r(&now, &timeinfo);
  DateTime ntpDateTime(
    timeinfo.tm_year + 1900,
    timeinfo.tm_mon + 1,
    timeinfo.tm_mday,
    timeinfo.tm_hour,
    timeinfo.tm_min,
    timeinfo.tm_sec
  );

  DateTime rtcNow = rtc.now();

  if (isRtcTimeValid(now, rtcNow, 60)) {
    addSerialLog("✅ RTC time is valid. No update needed.");
  } else {
    rtc.adjust(ntpDateTime);
    char buf[32];
    strftime(buf, sizeof(buf), "%Y-%m-%d %I:%M:%S %p", &timeinfo);
    addSerialLog("⚠️ RTC time corrected via NTP: " + String(buf));
  }
}

void activateAPModeFromMenu() {
  // Check if AP is already running
  if (WiFi.getMode() == WIFI_AP) {
    String apIP = WiFi.softAPIP().toString();
    addSerialLog("AP Mode already running. SSID: " + String(AP_SSID_MENU) +
                 ", PASS: " + String(AP_PASS) + ", IP: " + apIP);
    showAPStatusScreen("AP Mode Already On", AP_SSID_MENU, AP_PASS, apIP);
    delay(1500); // Brief message
    return;
  }

  // Start AP mode
  WiFi.disconnect(true);
  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID_MENU, AP_PASS);

  String apIP = WiFi.softAPIP().toString();
  addSerialLog("AP Mode started from menu. SSID: " + String(AP_SSID_MENU) +
               ", PASS: " + String(AP_PASS) + ", IP: " + apIP);

  setupServerRoutes_AP(apIP, AP_SSID_MENU, AP_PASS);
  server.begin();

  showAPStatusScreen("AP Mode Started", AP_SSID_MENU, AP_PASS, apIP);
}

void deactivateAPModeFromMenu() {
  // Check if AP mode is already off
  if (WiFi.getMode() != WIFI_AP) {
    addSerialLog("AP Mode already off.");
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0);
    display.println("AP Mode Already Off");
    display.display();
    delay(1500); // Show message briefly
    return;
  }

  // Stop AP mode
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_OFF);

  addSerialLog("AP Mode stopped from menu.");
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println("AP Mode Stopped");
  display.display();
  delay(1500); // Show message briefly
}


// ======================= Setup and Loop =======================
void waitMillis(unsigned long ms) {
  unsigned long start = millis();
  while (millis() - start < ms) {
    Blynk.run();   // if you're using Blynk
    timer.run();   // if using SimpleTimer or BlynkTimer
    yield();       // important for ESP8266/ESP32 to avoid watchdog resets
  }
}

void setup() {
  Serial.begin(115200);
  Wire.begin();

  wifiSetupSkipped = (readFloat(ADDR_WIFI_SKIP_F) > 0.5f);

  // Initialize OLED
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println(F("SSD1306 allocation failed"));
    for (;;);
  }
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println("Initializing...");
  display.display();

  showBootProgressBar();
  showWelcomeScreen();

  // Initialize buttons
  pinMode(BACK_BUTTON_PIN, INPUT_PULLUP);
  pinMode(SELECT_BUTTON_PIN, INPUT_PULLUP);
  pinMode(UP_BUTTON_PIN, INPUT_PULLUP);
  pinMode(DOWN_BUTTON_PIN, INPUT_PULLUP);

  // Load WiFi credentials
  loadWiFiCredentials();

  bool wifiConnected = false;

  if (strlen(savedSsid) == 0) {
    Serial.println("No saved WiFi credentials. Starting AP mode...");
    startAPMode();
  } else {
    connectWiFi();

    // Non-blocking wait for WiFi connection (max 10s)
    unsigned long wifiStart = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - wifiStart < 10000) {
      Blynk.run();
      timer.run();
      yield();

      display.clearDisplay();
      display.setCursor(0, 0);
      display.println("Connecting to WiFi...");
      display.display();
    }

    if (WiFi.status() != WL_CONNECTED) {
      Serial.println("WiFi connection failed. Switching to AP mode...");
      startAPMode();
    } else {
      wifiConnected = true;
    }
  }

  setupServerRoutes();
  server.begin();

  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(0, 0);

if (WiFi.status() == WL_CONNECTED) {
  if (isInternetAvailable()) {
    display.println("WiFi + Internet OK");
  } else {
    display.println("WiFi OK, No Internet");
  }

  display.print("IP: ");
  display.println(WiFi.localIP());
} else {
  display.println("AP Mode Active");
  display.print("SSID: ");
  display.println(AP_SSID);
  display.print("IP: ");
  display.println(WiFi.softAPIP());
}
    display.display();


  // Sensors initialization
  ina219_present = ina219.begin();
  if (ina219_present) ina219.setCalibration_32V_2A();

  ads1115_present = ads.begin();
  if (ads1115_present) ads.setGain(GAIN_ONE);

  rtc_present = rtc.begin();
  if (!rtc_present) Serial.println("Couldn't find RTC");

  if (wifiConnected && rtc_present) {
    syncTimeFromNTPtoRTC();
  }

  // Load EEPROM values
  readFloat(ADDR_BATTERY_CAPACITY, &batteryCapacityAh);
  readFloat(ADDR_VOLTAGE_THRESHOLD_MIN, &minVoltageThreshold);
  readFloat(ADDR_VOLTAGE_THRESHOLD_MAX, &maxVoltageThreshold);
  readFloat(ADDR_CURRENT_DEADZONE, &currentDeadzoneThreshold);
  readInt(ADDR_SCREEN_TIMEOUT, &screenTimeout);
  readInt(ADDR_STATS_CYCLE_COUNT, &cycleCount);
  readFloat(ADDR_STATS_TOTAL_ENERGY_IN, &totalEnergyInWh);
  readFloat(ADDR_STATS_TOTAL_ENERGY_OUT, &totalEnergyOutWh);
  readInt(ADDR_CHARGING_SECONDS, &chargingSeconds);
  readInt(ADDR_DISCHARGING_SECONDS, &dischargingSeconds);
  readInt(ADDR_IDLE_SECONDS, &idleSeconds);

  lastStateChangeMillis = millis();
  lastRuntimeSaveMillis = millis();

  uint32_t socFlag;
  readInt(ADDR_SOC_SAVED_FLAG, &socFlag);
  if (socFlag == 1) {
    readFloat(ADDR_SOC, &soc);
  } else {
    soc = 100.0;
    writeFloat(ADDR_SOC, soc);
    writeInt(ADDR_SOC_SAVED_FLAG, 1);
  }

  totalCoulombs = (soc / 100.0) * batteryCapacityAh * 3600.0;
  lastUpdate = millis();
  lastActivityTime = millis();
  lastActiveStateChange = millis();
  saverX = SCREEN_WIDTH / 2;
  saverY = SCREEN_HEIGHT / 2;

  // Calibrate zero current (non-blocking delay)
  Serial.println("Calibrating zero current...");
  waitMillis(5000); // replaced delay(5000)

  long total = 0;
  for (int i = 0; i < MEASUREMENT_ITERATIONS; i++) {
    int16_t adcReading = ads.readADC_SingleEnded(SENSOR_CHANNEL);
    float mV = ads.computeVolts(adcReading) * 1000.0;
    total += mV;
    waitMillis(2);  // replaced delay(2)
  }
  zeroOffset_mV = (float)total / MEASUREMENT_ITERATIONS;
  Serial.print("Zero Current Offset (mV): ");
  Serial.println(zeroOffset_mV, 3);

  // Set timers
  timer.setInterval(1000L, sendToBlynk);
  timer.setInterval(300000L, saveSocToEEPROM);
  timer.setInterval(600000L, saveEnergyStatsToEEPROM);
  timer.setInterval(60000L, checkAndResetDailyEnergy);
  timer.setInterval(5000L, updateBlynkBackupTime);
  timer.setInterval(5000L, updateBlynkChargingTime);

  Serial.println("Waiting for sensor to stabilize...");
  waitMillis(5000); // replaced delay(5000)
  isSensorStable = true;

  // Load current thresholds
  readFloat(ADDR_CHARGING_THRESHOLD, &chargingCurrentThreshold);
  if (isnan(chargingCurrentThreshold) || chargingCurrentThreshold <= 0.0 || chargingCurrentThreshold > 10.0)
    chargingCurrentThreshold = 0.6;

  readFloat(ADDR_DISCHARGING_THRESHOLD, &dischargingCurrentThreshold);
  if (isnan(dischargingCurrentThreshold) || dischargingCurrentThreshold <= 0.0 || dischargingCurrentThreshold > 10.0)
    dischargingCurrentThreshold = 1.0;

  Serial.println("Sensor stable. Starting measurements.");
}




void sendToBlynk() {
  static bool toggleHalf = false;

  if (!Blynk.connected()) return;

  // 🟢 Always send critical values (every 1 second)
  Blynk.virtualWrite(V0, currentVoltage);       // Voltage
  Blynk.virtualWrite(V1, filteredCurrent);      // Current
  Blynk.virtualWrite(V3, soc);                  // SOC

  // 🔁 Send secondary data every 2 seconds (alternating)
  if (toggleHalf) {
    Blynk.virtualWrite(V2, currentPower);       // Power
    Blynk.virtualWrite(V5, totalEnergyInWh);    // Energy In
    Blynk.virtualWrite(V6, totalEnergyOutWh);   // Energy Out

    // Battery Status
    String status = "Idle";
			if (filteredCurrent > chargingCurrentThreshold) {
				status = "Charging";
			} else if (filteredCurrent < -dischargingCurrentThreshold) {
				status = "Discharging";
			}
    Blynk.virtualWrite(V4, status);             // Status

    // Uptime
    unsigned long seconds = uptimeSeconds % 60;
    unsigned long minutes = (uptimeSeconds / 60) % 60;
    unsigned long hours   = (uptimeSeconds / 3600) % 24;
    unsigned long days    = uptimeSeconds / 86400;

    char uptimeStr[40];
    sprintf(uptimeStr, "%lud %luh %lum %lus", days, hours, minutes, seconds);
    Blynk.virtualWrite(V7, uptimeStr);
  }

  toggleHalf = !toggleHalf;  // Flip each second
}

void processButtons() {
    buttonUpPressed     = !digitalRead(UP_BUTTON_PIN);
    buttonDownPressed   = !digitalRead(DOWN_BUTTON_PIN);
    buttonSelectPressed = !digitalRead(SELECT_BUTTON_PIN);
    buttonBackPressed   = !digitalRead(BACK_BUTTON_PIN);

    // Wake screen if off
    if (!screenIsOn && (buttonUpPressed || buttonDownPressed || buttonSelectPressed || buttonBackPressed)) {
        screenIsOn = true;
        display.ssd1306_command(SSD1306_DISPLAYON);
        lastActivityTime = millis();
        currentMenuState = STATE_MAIN_DISPLAY;
        return;
    }

    // Debounce guard
    if (!screenIsOn || millis() - lastButtonPressTime < debounceDelay) {
        return;
    }

    // Special handling for charging/discharging threshold states
    if (currentMenuState == STATE_SET_CHARGING_THRESHOLD) {
        if (buttonUpPressed)    { tempFloatValue += 0.1; lastButtonPressTime = millis(); }
        if (buttonDownPressed)  { tempFloatValue = max(0.1f, tempFloatValue - 0.1f); lastButtonPressTime = millis(); }
        if (buttonSelectPressed) {
            chargingCurrentThreshold = tempFloatValue;
            writeFloat(ADDR_CHARGING_THRESHOLD, chargingCurrentThreshold);
            currentMenuState = STATE_MESSAGE;
            tempMessage = "Charging threshold saved.";
            messageDisplayStartTime = millis();
            popHistory();
        }
        if (buttonBackPressed) {
            popHistory();
            currentMenuState = menuHistory[historyIndex].state;
            selectedMenuIndex = menuHistory[historyIndex].selectedIndex;
        }
        return;
    }

    if (currentMenuState == STATE_SET_DISCHARGING_THRESHOLD) {
        if (buttonUpPressed)    { tempFloatValue += 0.1; lastButtonPressTime = millis(); }
        if (buttonDownPressed)  { tempFloatValue = max(0.1f, tempFloatValue - 0.1f); lastButtonPressTime = millis(); }
        if (buttonSelectPressed) {
            dischargingCurrentThreshold = tempFloatValue;
            writeFloat(ADDR_DISCHARGING_THRESHOLD, dischargingCurrentThreshold);
            currentMenuState = STATE_MESSAGE;
            tempMessage = "Discharging threshold saved.";
            messageDisplayStartTime = millis();
            popHistory();
        }
        if (buttonBackPressed) {
            popHistory();
            currentMenuState = menuHistory[historyIndex].state;
            selectedMenuIndex = menuHistory[historyIndex].selectedIndex;
        }
        return;
    }

    // Update activity time
    if (buttonUpPressed || buttonDownPressed || buttonSelectPressed || buttonBackPressed) {
        lastActivityTime = millis();
    }

    // --- Main Display ---
    if (currentMenuState == STATE_MAIN_DISPLAY) {
        if (buttonSelectPressed) {
            pushHistory(currentMenuState, selectedMenuIndex);
            currentMenuState = STATE_MAIN_MENU;
            selectedMenuIndex = 0;
            menuScrollOffset = 0;
            lastButtonPressTime = millis();
        }
    }

    // --- Main Menu ---
    else if (currentMenuState == STATE_MAIN_MENU) {
        if (buttonUpPressed) {
            if (selectedMenuIndex > 0) {
                selectedMenuIndex--;
                if (selectedMenuIndex < menuScrollOffset) menuScrollOffset = selectedMenuIndex;
            }
            lastButtonPressTime = millis();
        }
        if (buttonDownPressed) {
            if (selectedMenuIndex < numMainMenuItems - 1) {
                selectedMenuIndex++;
                if (selectedMenuIndex >= menuScrollOffset + visibleMenuItems) {
                    menuScrollOffset = selectedMenuIndex - visibleMenuItems + 1;
                }
            }
            lastButtonPressTime = millis();
        }
        if (buttonSelectPressed) {
            pushHistory(currentMenuState, selectedMenuIndex);
            switch (selectedMenuIndex) {
                case 0: currentMenuState = STATE_MAIN_DISPLAY; break;
                case 1: currentMenuState = STATE_CONFIG_MENU; selectedMenuIndex = 0; menuScrollOffset = 0; break;
                case 2: currentMenuState = STATE_CALIBRATION_MENU; selectedMenuIndex = 0; menuScrollOffset = 0; break;
                case 3: currentMenuState = STATE_STATS_MENU; selectedMenuIndex = 0; menuScrollOffset = 0; break;
                case 4: currentMenuState = STATE_SYSTEM_INFO_MENU; selectedMenuIndex = 0; menuScrollOffset = 0; break;
                case 5: currentMenuState = STATE_VIEW_QR_CODE; break;
                case 6: currentMenuState = STATE_AP_MODE_MENU; selectedMenuIndex = 0; menuScrollOffset = 0; break;
            }
            lastButtonPressTime = millis();
        }
        if (buttonBackPressed) {
            popHistory();
            currentMenuState = menuHistory[historyIndex].state;
            selectedMenuIndex = menuHistory[historyIndex].selectedIndex;
            lastButtonPressTime = millis();
        }
    }

    // --- AP Mode Menu ---
    else if (currentMenuState == STATE_AP_MODE_MENU) {
        if (buttonUpPressed) {
            if (selectedMenuIndex > 0) {
                selectedMenuIndex--;
                if (selectedMenuIndex < menuScrollOffset) menuScrollOffset = selectedMenuIndex;
            }
            lastButtonPressTime = millis();
        }
        if (buttonDownPressed) {
            if (selectedMenuIndex < apModeMenuCount - 1) {
                selectedMenuIndex++;
                if (selectedMenuIndex >= menuScrollOffset + visibleMenuItems) {
                    menuScrollOffset = selectedMenuIndex - visibleMenuItems + 1;
                }
            }
            lastButtonPressTime = millis();
        }
        if (buttonSelectPressed) {
            pushHistory(currentMenuState, selectedMenuIndex);
            switch (selectedMenuIndex) {
                case 0: activateAPModeFromMenu(); break;
                case 1: deactivateAPModeFromMenu(); break;
                case 2: popHistory();
                        currentMenuState = menuHistory[historyIndex].state;
                        selectedMenuIndex = menuHistory[historyIndex].selectedIndex;
                        break;
            }
            lastButtonPressTime = millis();
        }
        if (buttonBackPressed) {
            popHistory();
            currentMenuState = menuHistory[historyIndex].state;
            selectedMenuIndex = menuHistory[historyIndex].selectedIndex;
            lastButtonPressTime = millis();
        }
    }

	else if (currentMenuState == STATE_CONFIG_MENU) {
		if (buttonUpPressed) {
			if (selectedMenuIndex > 0) {
				selectedMenuIndex--;
				if (selectedMenuIndex < menuScrollOffset) {
					menuScrollOffset = selectedMenuIndex;
				}
			}
			lastButtonPressTime = millis();
		}
		if (buttonDownPressed) {
			if (selectedMenuIndex < numConfigMenuItems - 1) {
				selectedMenuIndex++;
				if (selectedMenuIndex >= menuScrollOffset + visibleMenuItems) {
					menuScrollOffset = selectedMenuIndex - visibleMenuItems + 1;
				}
			}
			lastButtonPressTime = millis();
		}
		if (buttonSelectPressed) {
			pushHistory(currentMenuState, selectedMenuIndex);
			switch (selectedMenuIndex) {
				case 0: // Battery Settings
					currentMenuState = STATE_BATTERY_SETTINGS_MENU;
					selectedMenuIndex = 0;
					menuScrollOffset = 0;
					break;
				case 1: // Set screen timeout
					currentMenuState = STATE_SET_SCREEN_TIMEOUT;
					tempIntValue = screenTimeout;
					break;
				case 2: // Back
					popHistory();
					currentMenuState = menuHistory[historyIndex].state;
					selectedMenuIndex = menuHistory[historyIndex].selectedIndex;
					break;
			}
			lastButtonPressTime = millis();
		}
		if (buttonBackPressed) {
    currentMenuState = STATE_MAIN_DISPLAY;
    selectedMenuIndex = 0;
    menuScrollOffset = 0;
    lastButtonPressTime = millis();
}

	} else if (currentMenuState == STATE_BATTERY_SETTINGS_MENU) {
		if (buttonUpPressed) {
			if (selectedMenuIndex > 0) {
				selectedMenuIndex--;
				if (selectedMenuIndex < menuScrollOffset) {
					menuScrollOffset = selectedMenuIndex;
				}
			}
			lastButtonPressTime = millis();
		}
		if (buttonDownPressed) {
			if (selectedMenuIndex < numBatterySettingsItems - 1) {
				selectedMenuIndex++;
				if (selectedMenuIndex >= menuScrollOffset + visibleMenuItems) {
					menuScrollOffset = selectedMenuIndex - visibleMenuItems + 1;
				}
			}
			lastButtonPressTime = millis();
		}
		if (buttonSelectPressed) {
			pushHistory(currentMenuState, selectedMenuIndex);
			switch (selectedMenuIndex) {
				case 0: // Set battery capacity (Ah)
					currentMenuState = STATE_SET_CAPACITY;
					tempFloatValue = batteryCapacityAh;
					break;
				case 1: // Set voltage thresholds (min/max)
					currentMenuState = STATE_SET_VOLTAGE_THRESHOLDS_MIN;
					tempFloatValue = minVoltageThreshold;
					break;
				case 2: // Select battery type
					currentMenuState = STATE_SET_BATTERY_TYPE;
					tempIntValue = batteryType;
					break;
				case 3: // Reset SOC to 100%
					soc = 100.0;
					totalCoulombs = (soc / 100.0) * batteryCapacityAh * 3600.0;
					writeFloat(ADDR_SOC, soc);
                    currentMenuState = STATE_MESSAGE;
                    tempMessage = "SOC reset to 100%.";
                    messageDisplayStartTime = millis();
					break;
				case 4: // Back
					popHistory();
					currentMenuState = menuHistory[historyIndex].state;
					selectedMenuIndex = menuHistory[historyIndex].selectedIndex;
					break;
			}
			lastButtonPressTime = millis();
		}
		if (buttonBackPressed) {
			popHistory();
			currentMenuState = menuHistory[historyIndex].state;
			selectedMenuIndex = menuHistory[historyIndex].selectedIndex;
			lastButtonPressTime = millis();
		}
	} else if (currentMenuState == STATE_SET_CAPACITY) {
		if (buttonUpPressed) {
			tempFloatValue += 0.1;
			lastButtonPressTime = millis();
		}
		if (buttonDownPressed) {
			tempFloatValue -= 0.1;
			tempFloatValue = max(0.1f, tempFloatValue);
			lastButtonPressTime = millis();
		}
		if (buttonSelectPressed) {
			batteryCapacityAh = tempFloatValue;
			writeFloat(ADDR_BATTERY_CAPACITY, batteryCapacityAh);
			totalCoulombs = (soc / 100.0) * batteryCapacityAh * 3600.0;
			popHistory();
			currentMenuState = STATE_MESSAGE;
            tempMessage = "Capacity saved.";
            messageDisplayStartTime = millis();
			lastButtonPressTime = millis();
		}
		if (buttonBackPressed) {
			popHistory();
			currentMenuState = menuHistory[historyIndex].state;
			selectedMenuIndex = menuHistory[historyIndex].selectedIndex;
			lastButtonPressTime = millis();
		}
	} else if (currentMenuState == STATE_SET_VOLTAGE_THRESHOLDS_MIN) {
		if (buttonUpPressed) {
			tempFloatValue += 0.1;
			lastButtonPressTime = millis();
		}
		if (buttonDownPressed) {
			tempFloatValue -= 0.1;
			tempFloatValue = max(0.1f, tempFloatValue);
			lastButtonPressTime = millis();
		}
		if (buttonSelectPressed) {
			minVoltageThreshold = tempFloatValue;
			pushHistory(currentMenuState, selectedMenuIndex);
			currentMenuState = STATE_SET_VOLTAGE_THRESHOLDS_MAX;
			tempFloatValue = maxVoltageThreshold;
			lastButtonPressTime = millis();
		}
		if (buttonBackPressed) {
			popHistory();
			currentMenuState = menuHistory[historyIndex].state;
			selectedMenuIndex = menuHistory[historyIndex].selectedIndex;
			lastButtonPressTime = millis();
		}
	} else if (currentMenuState == STATE_SET_VOLTAGE_THRESHOLDS_MAX) {
		if (buttonUpPressed) {
			tempFloatValue += 0.1;
			lastButtonPressTime = millis();
		}
		if (buttonDownPressed) {
			tempFloatValue -= 0.1;
			tempFloatValue = max(minVoltageThreshold + 0.1f, tempFloatValue);
			lastButtonPressTime = millis();
		}
		if (buttonSelectPressed) {
			maxVoltageThreshold = tempFloatValue;
			writeFloat(ADDR_VOLTAGE_THRESHOLD_MIN, minVoltageThreshold);
			writeFloat(ADDR_VOLTAGE_THRESHOLD_MAX, maxVoltageThreshold);
			popHistory();
			popHistory();
			currentMenuState = STATE_MESSAGE;
            tempMessage = "Voltage thresholds saved.";
            messageDisplayStartTime = millis();
			lastButtonPressTime = millis();
		}
		if (buttonBackPressed) {
			popHistory();
			currentMenuState = menuHistory[historyIndex].state;
			selectedMenuIndex = menuHistory[historyIndex].selectedIndex;
			lastButtonPressTime = millis();
		}
	} else if (currentMenuState == STATE_SET_BATTERY_TYPE) {
		if (buttonUpPressed) {
			if (tempIntValue > 0) tempIntValue--;
			lastButtonPressTime = millis();
		}
		if (buttonDownPressed) {
			if (tempIntValue < 2) tempIntValue++;
			lastButtonPressTime = millis();
		}
		if (buttonSelectPressed) {
			batteryType = tempIntValue;
			writeFloat(ADDR_BATTERY_TYPE, batteryType);
			popHistory();
			currentMenuState = STATE_MESSAGE;
            tempMessage = "Battery type saved.";
            messageDisplayStartTime = millis();
			lastButtonPressTime = millis();
		}
		if (buttonBackPressed) {
			popHistory();
			currentMenuState = menuHistory[historyIndex].state;
			selectedMenuIndex = menuHistory[historyIndex].selectedIndex;
			lastButtonPressTime = millis();
		}
	} else if (currentMenuState == STATE_SET_SCREEN_TIMEOUT) {
		if (buttonUpPressed) {
			tempIntValue += 5;
			tempIntValue = constrain(tempIntValue, 5, 300);
			lastButtonPressTime = millis();
		}
		if (buttonDownPressed) {
			tempIntValue -= 5;
			tempIntValue = constrain(tempIntValue, 5, 300);
			lastButtonPressTime = millis();
		}
		if (buttonSelectPressed) {
			screenTimeout = tempIntValue;
			writeInt(ADDR_SCREEN_TIMEOUT, screenTimeout);
			popHistory();
			currentMenuState = STATE_MESSAGE;
            tempMessage = "Screen timeout saved.";
            messageDisplayStartTime = millis();
			lastButtonPressTime = millis();
		}
		if (buttonBackPressed) {
			popHistory();
			currentMenuState = menuHistory[historyIndex].state;
			selectedMenuIndex = menuHistory[historyIndex].selectedIndex;
			lastButtonPressTime = millis();
		}
	} else if (currentMenuState == STATE_CALIBRATION_MENU) {
  if (buttonUpPressed) {
    if (selectedMenuIndex > 0) {
      selectedMenuIndex--;
      if (selectedMenuIndex < menuScrollOffset) {
        menuScrollOffset = selectedMenuIndex;
      }
    }
    lastButtonPressTime = millis();
  }

  if (buttonDownPressed) {
    if (selectedMenuIndex < numCalibrationMenuItems - 1) {
      selectedMenuIndex++;
      if (selectedMenuIndex >= menuScrollOffset + visibleMenuItems) {
        menuScrollOffset = selectedMenuIndex - visibleMenuItems + 1;
      }
    }
    lastButtonPressTime = millis();
  }

  if (buttonSelectPressed) {
    pushHistory(currentMenuState, selectedMenuIndex);
    switch (selectedMenuIndex) {
      case 0: // Current Sensor Calibration
        currentMenuState = STATE_CURRENT_CAL_MENU;
        selectedMenuIndex = 0;
        menuScrollOffset = 0;
        break;
      case 1: // Voltage Calibration
        currentMenuState = STATE_VOLTAGE_CAL_MENU;
        selectedMenuIndex = 0;
        menuScrollOffset = 0;
        break;
      case 2: // Save / Load Calibration
        currentMenuState = STATE_SAVE_LOAD_CAL_MENU;
        selectedMenuIndex = 0;
        menuScrollOffset = 0;
        break;
      case 3: // Back
        popHistory();
        currentMenuState = menuHistory[historyIndex].state;
        selectedMenuIndex = menuHistory[historyIndex].selectedIndex;
        break;
    }
    lastButtonPressTime = millis();
  }

  if (buttonBackPressed) {
    popHistory();
    currentMenuState = menuHistory[historyIndex].state;
    selectedMenuIndex = menuHistory[historyIndex].selectedIndex;
    lastButtonPressTime = millis();
  }
} else if (currentMenuState == STATE_CURRENT_CAL_MENU) {
  if (buttonUpPressed) {
    if (selectedMenuIndex > 0) {
      selectedMenuIndex--;
      if (selectedMenuIndex < menuScrollOffset) {
        menuScrollOffset = selectedMenuIndex;
      }
    }
    lastButtonPressTime = millis();
  }

  if (buttonDownPressed) {
    if (selectedMenuIndex < numCurrentSensorCalItems - 1) {
      selectedMenuIndex++;
      if (selectedMenuIndex >= menuScrollOffset + visibleMenuItems) {
        menuScrollOffset = selectedMenuIndex - visibleMenuItems + 1;
      }
    }
    lastButtonPressTime = millis();
  }

  if (buttonSelectPressed) {
    pushHistory(currentMenuState, selectedMenuIndex);
    switch (selectedMenuIndex) {
      case 0: // Auto-zero current sensor
        recalibrateZeroADC();
        popHistory(); popHistory();
        break;

      case 1: // Manual zero offset adjustment
        currentMenuState = STATE_MANUAL_ZERO_OFFSET;
        tempFloatValue = currentOffset;
        break;

		case 2: // Set Charge Current Threshold
		if (isnan(chargingCurrentThreshold) || chargingCurrentThreshold <= 0.0 || chargingCurrentThreshold > 10.0)
			chargingCurrentThreshold = 0.6;
		tempFloatValue = chargingCurrentThreshold;
		currentMenuState = STATE_SET_CHARGING_THRESHOLD;
		break;

		case 3: // Set Discharge Current Threshold
		if (isnan(dischargingCurrentThreshold) || dischargingCurrentThreshold <= 0.0 || dischargingCurrentThreshold > 10.0)
			dischargingCurrentThreshold = 1.0;
		tempFloatValue = dischargingCurrentThreshold;
		currentMenuState = STATE_SET_DISCHARGING_THRESHOLD;
		break;
		
    case 4: // Set mV per Amp value
        currentMenuState = STATE_SET_MV_PER_AMP;
        tempFloatValue = mVperAmp;
        break;

      case 5: // Back
        popHistory();
        currentMenuState = menuHistory[historyIndex].state;
        selectedMenuIndex = menuHistory[historyIndex].selectedIndex;
        break;
    }
    lastButtonPressTime = millis();
  }

  if (buttonBackPressed) {
    popHistory();
    currentMenuState = menuHistory[historyIndex].state;
    selectedMenuIndex = menuHistory[historyIndex].selectedIndex;
    lastButtonPressTime = millis();
  }
} else if (currentMenuState == STATE_MANUAL_ZERO_OFFSET) {
		if (buttonUpPressed) {
			tempFloatValue += 0.01;
			lastButtonPressTime = millis();
		}
		if (buttonDownPressed) {
			tempFloatValue -= 0.01;
			lastButtonPressTime = millis();
		}
		if (buttonSelectPressed) {
			currentOffset = tempFloatValue;
			writeFloat(ADDR_CURRENT_OFFSET, currentOffset);
			popHistory();
			currentMenuState = STATE_MESSAGE;
            tempMessage = "Current offset saved.";
            messageDisplayStartTime = millis();
			lastButtonPressTime = millis();
		}
		if (buttonBackPressed) {
			popHistory();
			currentMenuState = menuHistory[historyIndex].state;
			selectedMenuIndex = menuHistory[historyIndex].selectedIndex;
			lastButtonPressTime = millis();
		}
	} else if (currentMenuState == STATE_SET_MV_PER_AMP) {
		if (buttonUpPressed) {
			tempFloatValue += 0.1;
			lastButtonPressTime = millis();
		}
		if (buttonDownPressed) {
			tempFloatValue -= 0.1;
			tempFloatValue = max(0.1f, tempFloatValue);
			lastButtonPressTime = millis();
		}
		if (buttonSelectPressed) {
			mVperAmp = tempFloatValue;
			writeFloat(ADDR_MV_PER_AMP, mVperAmp);
			popHistory();
			currentMenuState = STATE_MESSAGE;
            tempMessage = "mV per Amp saved.";
            messageDisplayStartTime = millis();
			lastButtonPressTime = millis();
		}
		if (buttonBackPressed) {
			popHistory();
			currentMenuState = menuHistory[historyIndex].state;
			selectedMenuIndex = menuHistory[historyIndex].selectedIndex;
			lastButtonPressTime = millis();
		}
	} else if (currentMenuState == STATE_VOLTAGE_CAL_MENU) {
		if (buttonUpPressed) {
			if (selectedMenuIndex > 0) {
				selectedMenuIndex--;
				if (selectedMenuIndex < menuScrollOffset) {
					menuScrollOffset = selectedMenuIndex;
				}
			}
			lastButtonPressTime = millis();
		}
		if (buttonDownPressed) {
			if (selectedMenuIndex < numVoltageCalItems - 1) {
				selectedMenuIndex++;
				if (selectedMenuIndex >= menuScrollOffset + visibleMenuItems) {
					menuScrollOffset = selectedMenuIndex - visibleMenuItems + 1;
				}
			}
			lastButtonPressTime = millis();
		}
		if (buttonSelectPressed) {
			pushHistory(currentMenuState, selectedMenuIndex);
			switch (selectedMenuIndex) {
				case 0: // Adjust voltage reading offset
					currentMenuState = STATE_ADJUST_VOLTAGE_OFFSET;
					tempFloatValue = voltageOffset;
					break;
				case 1: // Calibrate with known voltage source
					currentMenuState = STATE_CALIBRATE_KNOWN_VOLTAGE;
					tempFloatValue = currentVoltage;
					break;
				case 2: // Back
					popHistory();
					currentMenuState = menuHistory[historyIndex].state;
					selectedMenuIndex = menuHistory[historyIndex].selectedIndex;
					break;
			}
			lastButtonPressTime = millis();
		}
		if (buttonBackPressed) {
			popHistory();
			currentMenuState = menuHistory[historyIndex].state;
			selectedMenuIndex = menuHistory[historyIndex].selectedIndex;
			lastButtonPressTime = millis();
		}
	} else if (currentMenuState == STATE_ADJUST_VOLTAGE_OFFSET) {
		if (buttonUpPressed) {
			tempFloatValue += 0.01;
			lastButtonPressTime = millis();
		}
		if (buttonDownPressed) {
			tempFloatValue -= 0.01;
			lastButtonPressTime = millis();
		}
		if (buttonSelectPressed) {
			voltageOffset = tempFloatValue;
			writeFloat(ADDR_VOLTAGE_OFFSET, voltageOffset);
			popHistory();
			currentMenuState = STATE_MESSAGE;
            tempMessage = "Voltage offset saved.";
            messageDisplayStartTime = millis();
			lastButtonPressTime = millis();
		}
		if (buttonBackPressed) {
			popHistory();
			currentMenuState = menuHistory[historyIndex].state;
			selectedMenuIndex = menuHistory[historyIndex].selectedIndex;
			lastButtonPressTime = millis();
		}
	} else if (currentMenuState == STATE_CALIBRATE_KNOWN_VOLTAGE) {
		if (buttonUpPressed) {
			tempFloatValue += 0.1;
			lastButtonPressTime = millis();
		}
		if (buttonDownPressed) {
			tempFloatValue -= 0.1;
			tempFloatValue = max(0.1f, tempFloatValue);
			lastButtonPressTime = millis();
		}
		if (buttonSelectPressed) {
			calibrateVoltageWithKnownSource(tempFloatValue);
			popHistory();
			popHistory();
			currentMenuState = STATE_MESSAGE;
            tempMessage = "Voltage calibrated.";
            messageDisplayStartTime = millis();
			lastButtonPressTime = millis();
		}
		if (buttonBackPressed) {
			popHistory();
			currentMenuState = menuHistory[historyIndex].state;
			selectedMenuIndex = menuHistory[historyIndex].selectedIndex;
			lastButtonPressTime = millis();
		}
	} else if (currentMenuState == STATE_SAVE_LOAD_CAL_MENU) {
		if (buttonUpPressed) {
			if (selectedMenuIndex > 0) {
				selectedMenuIndex--;
				if (selectedMenuIndex < menuScrollOffset) {
					menuScrollOffset = selectedMenuIndex;
				}
			}
			lastButtonPressTime = millis();
		}
		if (buttonDownPressed) {
			if (selectedMenuIndex < numSaveLoadCalItems - 1) {
				selectedMenuIndex++;
				if (selectedMenuIndex >= menuScrollOffset + visibleMenuItems) {
					menuScrollOffset = selectedMenuIndex - visibleMenuItems + 1;
				}
			}
			lastButtonPressTime = millis();
		}
		if (buttonSelectPressed) {
			pushHistory(currentMenuState, selectedMenuIndex);
			switch (selectedMenuIndex) {
				case 0: // Save to EEPROM
					saveCalibration();
					popHistory(); // Pop the menu state
					popHistory(); // Pop the calibration menu state
					break;
				case 1: // Load from EEPROM
					loadCalibration();
					popHistory(); // Pop the menu state
					popHistory(); // Pop the calibration menu state
					break;
				case 2: // Reset to defaults
					resetCalibrationDefaults();
					popHistory(); // Pop the menu state
					popHistory(); // Pop the calibration menu state
					break;
				case 3: // Back
					popHistory();
					currentMenuState = menuHistory[historyIndex].state;
					selectedMenuIndex = menuHistory[historyIndex].selectedIndex;
					break;
			}
			lastButtonPressTime = millis();
		}
		if (buttonBackPressed) {
			popHistory();
			currentMenuState = menuHistory[historyIndex].state;
			selectedMenuIndex = menuHistory[historyIndex].selectedIndex;
			lastButtonPressTime = millis();
		}
	} else if (currentMenuState == STATE_STATS_MENU) {
		if (buttonUpPressed) {
			if (selectedMenuIndex > 0) {
				selectedMenuIndex--;
				if (selectedMenuIndex < menuScrollOffset) {
					menuScrollOffset = selectedMenuIndex;
				}
			}
			lastButtonPressTime = millis();
		}
		if (buttonDownPressed) {
			if (selectedMenuIndex < numStatsMenuItems - 1) {
				selectedMenuIndex++;
				if (selectedMenuIndex >= menuScrollOffset + visibleMenuItems) {
					menuScrollOffset = selectedMenuIndex - visibleMenuItems + 1;
				}
			}
			lastButtonPressTime = millis();
		}
		if (buttonSelectPressed) {
			pushHistory(currentMenuState, selectedMenuIndex);
			switch (selectedMenuIndex) {
				case 0: // Cycle Count
					currentMenuState = STATE_VIEW_CYCLE_COUNT;
					break;
				case 1: // Total Energy (Wh)
					currentMenuState = STATE_VIEW_TOTAL_ENERGY;
					break;
				case 2: // Runtime History
					currentMenuState = STATE_VIEW_RUNTIME_HISTORY;
					logViewOffset = 0;
					break;
				case 3: // Reset Statistics
					resetStatistics();
					popHistory(); // Pop menu state
					break;
				case 4: // Back
					popHistory();
					currentMenuState = menuHistory[historyIndex].state;
					selectedMenuIndex = menuHistory[historyIndex].selectedIndex;
					break;
			}
			lastButtonPressTime = millis();
		}
		if (buttonBackPressed) {
			popHistory();
			currentMenuState = menuHistory[historyIndex].state;
			selectedMenuIndex = menuHistory[historyIndex].selectedIndex;
			lastButtonPressTime = millis();
		}
	} else if (currentMenuState == STATE_VIEW_CYCLE_COUNT) {
		if (buttonBackPressed) {
			popHistory();
			currentMenuState = menuHistory[historyIndex].state;
			selectedMenuIndex = menuHistory[historyIndex].selectedIndex;
			lastButtonPressTime = millis();
		}
	} else if (currentMenuState == STATE_VIEW_TOTAL_ENERGY) {
		if (buttonBackPressed) {
			popHistory();
			currentMenuState = menuHistory[historyIndex].state;
			selectedMenuIndex = menuHistory[historyIndex].selectedIndex;
			lastButtonPressTime = millis();
		}
	} else if (currentMenuState == STATE_VIEW_RUNTIME_HISTORY) {
		if (buttonUpPressed) {
			if (logViewOffset < MAX_LOGS - visibleMenuItems) {
				logViewOffset++;
			}
			lastButtonPressTime = millis();
		}
		if (buttonDownPressed) {
			if (logViewOffset > 0) {
				logViewOffset--;
			}
			lastButtonPressTime = millis();
		}
		if (buttonBackPressed) {
			popHistory();
			currentMenuState = menuHistory[historyIndex].state;
			selectedMenuIndex = menuHistory[historyIndex].selectedIndex;
			lastButtonPressTime = millis();
		}
	} else if (currentMenuState == STATE_SYSTEM_INFO_MENU) {
		if (buttonUpPressed) {
			if (selectedMenuIndex > 0) {
				selectedMenuIndex--;
				if (selectedMenuIndex < menuScrollOffset) {
					menuScrollOffset = selectedMenuIndex;
				}
			}
			lastButtonPressTime = millis();
		}
		if (buttonDownPressed) {
			if (selectedMenuIndex < numSystemInfoItems - 1) {
				selectedMenuIndex++;
				if (selectedMenuIndex >= menuScrollOffset + visibleMenuItems) {
					menuScrollOffset = selectedMenuIndex - visibleMenuItems + 1;
				}
			}
			lastButtonPressTime = millis();
		}
		if (buttonSelectPressed) {
			pushHistory(currentMenuState, selectedMenuIndex);
			switch (selectedMenuIndex) {
				case 0: // Firmware Version
					currentMenuState = STATE_VIEW_FIRMWARE;
					break;
				case 1: // Sensor Status
					currentMenuState = STATE_VIEW_SENSOR_STATUS;
					break;
				case 2: // Memory Usage
					currentMenuState = STATE_VIEW_MEMORY_USAGE;
					break;
				case 3: // Uptime
					currentMenuState = STATE_VIEW_UPTIME;
					break;
				case 4: // About
					currentMenuState = STATE_ABOUT_MENU;
					break;
				case 5: // Back
					popHistory();
					currentMenuState = menuHistory[historyIndex].state;
					selectedMenuIndex = menuHistory[historyIndex].selectedIndex;
					break;
			}
			lastButtonPressTime = millis();
		}
		if (buttonBackPressed) {
			popHistory();
			currentMenuState = menuHistory[historyIndex].state;
			selectedMenuIndex = menuHistory[historyIndex].selectedIndex;
			lastButtonPressTime = millis();
		}
	} else if (currentMenuState == STATE_VIEW_FIRMWARE || currentMenuState == STATE_VIEW_SENSOR_STATUS ||
			   currentMenuState == STATE_VIEW_MEMORY_USAGE || currentMenuState == STATE_VIEW_UPTIME ||
			   currentMenuState == STATE_ABOUT_MENU) {
		if (buttonBackPressed) {
			popHistory();
			currentMenuState = menuHistory[historyIndex].state;
			selectedMenuIndex = menuHistory[historyIndex].selectedIndex;
			lastButtonPressTime = millis();
		}
	}
}

void handleMessageState() {
    if (millis() - messageDisplayStartTime > messageDuration) {
        MenuHistory prev = popHistory();
        currentMenuState = prev.state;
        selectedMenuIndex = prev.selectedIndex;
    }
}

unsigned long lastLogTime = 0;
void loop() {
    // Keep WiFi / HTTP responsive
    server.handleClient();

    // Check for internet every 30 min ONLY IF internet was previously down
  if (!internetConnected && millis() - lastInternetCheck >= INTERNET_CHECK_INTERVAL) {
    lastInternetCheck = millis();

    Serial.println("🔁 Rechecking internet connectivity...");

    if (WiFi.status() == WL_CONNECTED && isInternetAvailable()) {
      internetConnected = true;
      addSerialLog("✅ Internet reconnected. Syncing services.");

      // Safe to run Blynk and NTP now
      Blynk.config(BLYNK_AUTH_TOKEN);
      Blynk.connect(5000);  // non-blocking

      checkAndFixRtcTime(); // if you use RTC/NTP

    } else {
      Serial.println("❌ Still no internet.");
      addSerialLog("Still no internet after retry.");
    }
  }
    // Blynk tasks
    Blynk.run();
    timer.run();

    DateTime rtcNow = rtc.now();

    // Logs every 60 seconds
    if (millis() - lastLogTime >= 10000) {
        logSystemStatus();
        logSensorStatus();
        lastLogTime = millis();
    }

    // Midnight RTC check
    static unsigned long lastMidnightCheck = 0;
    if (rtcNow.hour() == 0 && rtcNow.minute() == 0 && rtcNow.second() == 0) {
      if (millis() - lastMidnightCheck > 1000) { // debounce 1 second
        checkAndFixRtcTime();
        lastMidnightCheck = millis();
      }
  }
    // Sensor update (with yield inside the sampling loop)
    updateSensors();
    yield();

    unsigned long now = millis();
    uptimeMillis = now;
    uptimeSeconds = uptimeMillis / 1000;

    // Screen timeout handling
    if (screenIsOn && now - lastActivityTime > screenTimeout * 1000) {
        screenIsOn = false;
        currentMenuState = STATE_SCREEN_SAVER;
    }

    // Throttle OLED redraws (every 200ms max)
    static unsigned long lastOledUpdate = 0;
    if (screenIsOn && now - lastOledUpdate >= 200) {
        switch (currentMenuState) {
            case STATE_MAIN_DISPLAY:
                drawMainScreen();
                break;
            case STATE_MAIN_MENU:
                drawMenu("Main Menu", mainMenuOptions, numMainMenuItems, selectedMenuIndex, menuScrollOffset);
                break;
            case STATE_VIEW_QR_CODE:
                drawQRCodeScreen();
                break;
            case STATE_CONFIG_MENU:
                drawMenu("Configuration", configMenuOptions, numConfigMenuItems, selectedMenuIndex, menuScrollOffset);
                break;
            case STATE_BATTERY_SETTINGS_MENU:
                drawMenu("Battery Settings", batterySettingsOptions, numBatterySettingsItems, selectedMenuIndex, menuScrollOffset);
                break;
            case STATE_SET_CAPACITY:
                drawValueScreen("Set Capacity (Ah)", tempFloatValue, 1, 0.1);
                break;
            case STATE_SET_VOLTAGE_THRESHOLDS_MIN:
                drawValueScreen("Set Min Voltage", tempFloatValue, 2, 0.1);
                break;
            case STATE_SET_VOLTAGE_THRESHOLDS_MAX:
                drawValueScreen("Set Max Voltage", tempFloatValue, 2, 0.1);
                break;
            case STATE_SET_BATTERY_TYPE:
                drawMenu("Set Battery Type", batteryTypes, 3, tempIntValue, 0);
                break;
            case STATE_SET_SCREEN_TIMEOUT:
                drawValueScreen("Set Timeout (s)", (float)tempIntValue, 0, 5.0);
                break;
            case STATE_CALIBRATION_MENU:
                drawMenu("Calibration Menu", calibrationMenuOptions, numCalibrationMenuItems, selectedMenuIndex, menuScrollOffset);
                break;
            case STATE_CURRENT_CAL_MENU:
                drawMenu("Current Calibration", currentSensorCalOptions, numCurrentSensorCalItems, selectedMenuIndex, menuScrollOffset);
                break;
            case STATE_MANUAL_ZERO_OFFSET:
                drawValueScreen("Current Offset", tempFloatValue, 2, 0.01);
                break;
            case STATE_SET_MV_PER_AMP:
                drawValueScreen("mV/Amp", tempFloatValue, 2, 0.1);
                break;
            case STATE_VOLTAGE_CAL_MENU:
                drawMenu("Voltage Calibration", voltageCalOptions, numVoltageCalItems, selectedMenuIndex, menuScrollOffset);
                break;
            case STATE_ADJUST_VOLTAGE_OFFSET:
                drawValueScreen("Voltage Offset", tempFloatValue, 2, 0.01);
                break;
            case STATE_CALIBRATE_KNOWN_VOLTAGE:
                drawValueScreen("Known Voltage", tempFloatValue, 2, 0.1);
                break;
            case STATE_SAVE_LOAD_CAL_MENU:
                drawMenu("Save/Load", saveLoadCalOptions, numSaveLoadCalItems, selectedMenuIndex, menuScrollOffset);
                break;
            case STATE_SET_CHARGING_THRESHOLD:
                drawValueScreen("Charging Curr Thresh", tempFloatValue, 2, 0.1);
                break;
            case STATE_SET_DISCHARGING_THRESHOLD:
                drawValueScreen("Dischg Curr Thresh", tempFloatValue, 2, 0.1);
                break;
            case STATE_STATS_MENU:
                drawMenu("Statistics", statsMenuOptions, numStatsMenuItems, selectedMenuIndex, menuScrollOffset);
                break;
            case STATE_VIEW_CYCLE_COUNT:
                display.clearDisplay();
                display.setTextSize(1);
                display.setTextColor(SSD1306_WHITE);
                display.setCursor(0, 0);
                display.println("Cycle Count");
                display.drawFastHLine(0, 12, SCREEN_WIDTH, SSD1306_WHITE);
                display.setTextSize(2);
                display.setCursor(0, 24);
                display.println(cycleCount);
                display.display();
                break;
            case STATE_VIEW_TOTAL_ENERGY:
                display.clearDisplay();
                display.setTextSize(1);
                display.setTextColor(SSD1306_WHITE);
                display.setCursor(0, 0);
                display.println("Total Energy");
                display.drawFastHLine(0, 12, SCREEN_WIDTH, SSD1306_WHITE);
                display.setCursor(0, 20);
                display.print("Total In: ");
                display.print(totalEnergyInWh, 2);
                display.setCursor(0, 32);
                display.print("Total Out: ");
                display.print(totalEnergyOutWh, 2);
                display.setCursor(0, 44);
                display.print("Reset in: ");
                display.print(getTimeUntilMidnight());
                display.display();
                break;
            case STATE_VIEW_RUNTIME_HISTORY:
                drawRuntimeHistoryScreen();
                break;
            case STATE_SYSTEM_INFO_MENU:
                drawMenu("System Info", systemInfoOptions, numSystemInfoItems, selectedMenuIndex, menuScrollOffset);
                break;
            case STATE_VIEW_FIRMWARE:
                display.clearDisplay();
                display.setTextSize(1);
                display.setTextColor(SSD1306_WHITE);
                display.setCursor(0, 0);
                display.println("Firmware");
                display.drawFastHLine(0, 12, SCREEN_WIDTH, SSD1306_WHITE);
                display.setCursor(0, 20);
                display.println(FIRMWARE_VERSION);
                display.display();
                break;
            case STATE_VIEW_SENSOR_STATUS:
                drawSystemInfoScreen();
                break;
            case STATE_VIEW_MEMORY_USAGE:
                drawMemoryUsageScreen();
                break;
            case STATE_VIEW_UPTIME:
                drawUptimeScreen();
                break;
            case STATE_ABOUT_MENU:
                drawAboutScreen();
                break;
            case STATE_MESSAGE:
                drawMessageScreen("System Message", tempMessage);
                handleMessageState();
                break;
            case STATE_AP_MODE_MENU:
                drawAPModeMenu(); // or showAPModeSubmenu(), whichever you named
                break;
        }
        lastOledUpdate = now;
    } else if (currentMenuState == STATE_SCREEN_SAVER) {
        drawScreenSaver();
    }

    // Handle buttons
    processButtons();

    // Keep server responsive after UI handling
    server.handleClient();
    yield();
}
