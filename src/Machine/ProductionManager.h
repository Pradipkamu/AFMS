#pragma once
#include <Arduino.h>

namespace ProductionManager {
void begin(uint8_t pin, uint32_t debounceUs = 50000UL);
bool consumePulse(uint32_t &timestampUs);
void acceptPulse();
uint32_t total();
void restore(uint32_t total);
void reset();
void setEnabled(bool enabled);
bool enabled();
uint32_t droppedPulseCount();
}
