#pragma once
#include <Arduino.h>

namespace ReliabilityManager {
void begin();
void update();
bool safeMode();
uint32_t minimumFreeHeap();
uint32_t lowHeapEvents();
const String &lastResetReason();
}
