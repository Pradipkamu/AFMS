#pragma once
#include <Arduino.h>

namespace RuntimeStateManager {
void begin();
void update();
bool saveNow();
bool restored();
uint32_t saveIntervalSeconds();
uint32_t saveSuccessCount();
uint32_t saveFailureCount();
}
