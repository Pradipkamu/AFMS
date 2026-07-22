#pragma once
#include <Arduino.h>

namespace CycleEndManager {
void begin(uint8_t pin, bool activeHigh, uint16_t debounceMs);
void update();
bool consumePulse(uint32_t &timestampUs);
void setEnabled(bool enabled);
void clearPending();
bool active();
uint32_t droppedPulseCount();
}
