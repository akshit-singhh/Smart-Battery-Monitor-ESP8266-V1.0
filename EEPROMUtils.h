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
