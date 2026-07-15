#pragma once
#include <Arduino.h>

namespace Config {
bool load();
bool save();
bool update(const String &machineId,
            const String &machineName,
            const String &wifiSsid,
            const String &wifiPassword,
            const String &googleWebAppUrl);
const char *machineId();
const char *machineName();
const char *wifiSsid();
const char *wifiPassword();
const char *googleWebAppUrl();
const char *apiToken();
const char *shiftName(uint8_t index);
const char *shiftStart(uint8_t index);
const char *shiftEnd(uint8_t index);
uint16_t shiftStartMinutes(uint8_t index);
uint16_t shiftEndMinutes(uint8_t index);
bool shiftScheduleValid();
}