#pragma once
#include <Arduino.h>

namespace RejectManager {
void begin(uint8_t pin, uint32_t debounceUs = 50000UL);
bool consumePulse();
uint32_t total();
void reset();
}
