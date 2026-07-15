#pragma once
#include <Arduino.h>

namespace PulseConfig {
void load();
uint32_t productionDebounceMs();
uint32_t rejectDebounceMs();
}