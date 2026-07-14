#pragma once
#include <Arduino.h>

namespace CycleManager {
void begin(uint32_t cycleTimeMs);
void setCycleTimeMs(uint32_t cycleTimeMs);
void onProduction(uint32_t nowMs);
uint32_t cycleTimeMs();
uint32_t lastProductionMs();
bool cycleExpired(uint32_t nowMs);
}
