#pragma once
#include <Arduino.h>

namespace CycleEndManager {
void begin(uint8_t pin, bool activeHigh, uint16_t debounceMs);
void update();
bool consumePulse(uint32_t &timestampMs);
bool active();
uint32_t droppedPulseCount();
}
