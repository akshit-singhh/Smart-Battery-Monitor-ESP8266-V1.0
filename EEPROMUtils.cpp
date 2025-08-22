/*
   Project   : Smart Battery Monitor (ESP8266 V1.0)
   File      : EEPROMUtils.cpp
   Author    : Akshit Singh (github.com/akshit-singhh)
   License   : MIT License

   Description:
   Utility functions for interacting with external EEPROM (AT24C32 on DS3231).
   Provides methods to persist and retrieve configuration and credentials.

   Functions:
   - writeFloat() / readFloat()
   - writeInt() / readInt()
   - writeString() / readString()

   Notes:
   - Values stored at predefined addresses (see AppServer.cpp)
   - Used for WiFi credentials, calibration values, SOC, thresholds, etc.
*/

#include "EEPROMUtils.h"
#include <Wire.h>

const uint8_t EEPROM_ADDR = 0x57; // AT24C32 I2C Address

// ------------------ Float Write ------------------
void writeFloat(uint16_t addr, float value) {
    Serial.print("💾 Writing to DS3231 EEPROM @ ");
    Serial.print(addr);
    Serial.print(" -> ");
    Serial.println(value, 4); // show with 4 decimal places

    byte* p = (byte*)&value;
    for (int i = 0; i < 4; i++) {
        Wire.beginTransmission(EEPROM_ADDR);
        Wire.write((uint8_t)((addr + i) >> 8));
        Wire.write((uint8_t)((addr + i) & 0xFF));
        Wire.write(p[i]);
        Wire.endTransmission();
        delay(5);
    }
}

// ------------------ Float Read (Pointer) ------------------
void readFloat(uint16_t addr, float* value) {
    Serial.print("📖 Reading from DS3231 EEPROM @ ");
    Serial.println(addr);

    byte data[4];
    for (int i = 0; i < 4; i++) {
        Wire.beginTransmission(EEPROM_ADDR);
        Wire.write((uint8_t)((addr + i) >> 8));
        Wire.write((uint8_t)((addr + i) & 0xFF));
        Wire.endTransmission();
        Wire.requestFrom((uint8_t)EEPROM_ADDR, (uint8_t)1);
        if (Wire.available()) {
            data[i] = Wire.read();
        }
    }
    memcpy(value, data, 4);
}

// ------------------ Float Read (Return) ------------------
float readFloat(uint16_t addr) {
    float val;
    readFloat(addr, &val);
    return val;
}

// ------------------ Int Write ------------------
void writeInt(uint16_t addr, uint32_t value) {
    byte* p = (byte*)&value;
    for (int i = 0; i < 4; i++) {
        Wire.beginTransmission(EEPROM_ADDR);
        Wire.write((uint8_t)((addr + i) >> 8));
        Wire.write((uint8_t)((addr + i) & 0xFF));
        Wire.write(p[i]);
        Wire.endTransmission();
        delay(5);
    }
}

// ------------------ Int Read (Pointer) ------------------
void readInt(uint16_t addr, uint32_t* value) {
    byte data[4];
    for (int i = 0; i < 4; i++) {
        Wire.beginTransmission(EEPROM_ADDR);
        Wire.write((uint8_t)((addr + i) >> 8));
        Wire.write((uint8_t)((addr + i) & 0xFF));
        Wire.endTransmission();
        Wire.requestFrom((uint8_t)EEPROM_ADDR, (uint8_t)1);
        if (Wire.available()) {
            data[i] = Wire.read();
        }
    }
    memcpy(value, data, 4);
}

// ------------------ Int Read (Return) ------------------
uint32_t readInt(uint16_t addr) {
    uint32_t val;
    readInt(addr, &val);
    return val;
}

// ------------------ Write String ------------------
void writeString(uint16_t addr, const char* value) {
    Wire.beginTransmission(EEPROM_ADDR);
    Wire.write((uint8_t)(addr >> 8));
    Wire.write((uint8_t)(addr & 0xFF));
    for (int i = 0; i < strlen(value); i++) {
        Wire.write(value[i]);
    }
    Wire.write(0x00); // Null terminator
    Wire.endTransmission();
    delay(5);
}

// ------------------ Read String ------------------
void readString(uint16_t addr, char* buffer, size_t size) {
    Wire.beginTransmission(EEPROM_ADDR);
    Wire.write((uint8_t)(addr >> 8));
    Wire.write((uint8_t)(addr & 0xFF));
    Wire.endTransmission();

    Wire.requestFrom((uint8_t)EEPROM_ADDR, (uint8_t)size);
    int i = 0;
    while (Wire.available() && i < size - 1) {
        buffer[i++] = Wire.read();
    }
    buffer[i] = '\0'; // Null-terminate
}
