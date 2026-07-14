#pragma once
#include <Arduino.h>

namespace TimeManager {
void begin(long utcOffsetSeconds = 19800, int daylightOffsetSeconds = 0);
void update();
bool synchronized();
time_t now();
String iso8601();
}
