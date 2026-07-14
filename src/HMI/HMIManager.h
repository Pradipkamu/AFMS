#pragma once
#include <Arduino.h>

namespace HMIManager {
void begin();
void update();
bool connected();
uint32_t requestCount();
uint32_t errorCount();
}
