#pragma once
#include <Arduino.h>

namespace AfmsWebClient {
void begin();
void update();
bool connected();
uint32_t successCount();
uint32_t failureCount();
}
