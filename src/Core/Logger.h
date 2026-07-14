#pragma once
#include <Arduino.h>

namespace Logger {
void begin(Stream &stream);
void info(const __FlashStringHelper *message);
void warn(const __FlashStringHelper *message);
void error(const __FlashStringHelper *message);
void info(const String &message);
}
