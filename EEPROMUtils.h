/*
   Project   : Smart Battery Monitor (ESP8266 V1.0)
   File      : EEPROMUtils.h
   Author    : Akshit Singh (github.com/akshit-singhh)
   License   : MIT License

   Description:
   Header file for EEPROMUtils.cpp.
   Declares EEPROM utility functions for reading and writing floats,
   integers, and strings.

   Supported APIs:
   - writeFloat(), readFloat()
   - writeInt(), readInt()
   - writeString(), readString()

   Notes:
   - Works with AT24C32 I2C EEPROM
   - Integrated with settings persistence across reboots
*/

#ifndef EEPROM_UTILS_H
#define EEPROM_UTILS_H

#include <Arduino.h>
#include <Wire.h>

// Original pointer-based APIs
void writeFloat(uint16_t addr, float value);
void readFloat(uint16_t addr, float* value);
void writeInt(uint16_t addr, uint32_t value);
void readInt(uint16_t addr, uint32_t* value);
void writeString(uint16_t addr, const char* value);
void readString(uint16_t addr, char* buffer, size_t size);

float readFloat(uint16_t addr);
uint32_t readInt(uint16_t addr);

#endif // EEPROM_UTILS_H
