#pragma once
#include <Arduino.h>

namespace CycleManager {
void begin(uint32_t cycleTimeMs, bool cycleEndEnabled, uint32_t cycleEndTimeoutMs);
void setCycleTimeMs(uint32_t cycleTimeMs);
void onProduction(uint32_t nowMs);
bool onCycleEnd(uint32_t nowMs);
void update(uint32_t nowMs);
uint32_t cycleTimeMs();
uint32_t lastProductionMs();
uint32_t completionTimeMs();
bool cycleCompleted();
bool cycleInProgress();
bool cycleEndEnabled();
uint32_t timeoutCount();
}
