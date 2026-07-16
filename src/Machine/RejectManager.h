#pragma once
#include <Arduino.h>

namespace RejectManager {
void begin(uint8_t pin, uint32_t debounceUs = 50000UL);
void setEnabled(bool enabled);
bool enabled();
bool consumePulse();
uint32_t total();
void restore(uint32_t total);
void reset();
}
