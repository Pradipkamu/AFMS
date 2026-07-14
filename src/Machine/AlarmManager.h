#pragma once
#include <Arduino.h>

namespace AlarmManager {
void begin(uint8_t pin, bool activeHigh = true);
void set(bool active);
void clear();
bool active();
}
