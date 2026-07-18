#pragma once

#include <Arduino.h>

namespace LossCatalog {
void begin();
const String &name(uint16_t lossCode);
bool loaded();
}
