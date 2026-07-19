#pragma once

#include <Arduino.h>

namespace CloudManager {
void begin();
void update();
void queueStatusNow();
bool connected();
uint32_t uploadSuccessCount();
uint32_t uploadFailureCount();
}
