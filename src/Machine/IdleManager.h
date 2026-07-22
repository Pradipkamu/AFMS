#pragma once
#include <Arduino.h>

namespace IdleManager {
void begin(uint32_t idleDelayMs);
void setIdleDelayMs(uint32_t idleDelayMs);
void onProduction();
void update(bool cycleCompleted, uint32_t nowMs, uint32_t completionTimeMs);
bool idle();
bool alarmDue();
uint32_t idleSeconds();
}
