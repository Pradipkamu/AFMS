#pragma once
#include <Arduino.h>

namespace OfflineQueue {
bool begin();
bool push(const String &record);
bool peek(String &record);
bool pop();
uint16_t count();
}
