#include "Logger.h"

namespace {
Stream *output = nullptr;

void write(const __FlashStringHelper *level, const String &message) {
  if (!output) return;
  output->print('[');
  output->print(millis());
  output->print(F("] "));
  output->print(level);
  output->print(F(": "));
  output->println(message);
}
}

void Logger::begin(Stream &stream) { output = &stream; }
void Logger::info(const __FlashStringHelper *message) { write(F("INFO"), String(message)); }
void Logger::warn(const __FlashStringHelper *message) { write(F("WARN"), String(message)); }
void Logger::error(const __FlashStringHelper *message) { write(F("ERROR"), String(message)); }
void Logger::info(const String &message) { write(F("INFO"), message); }
void Logger::warn(const String &message) { write(F("WARN"), message); }
void Logger::error(const String &message) { write(F("ERROR"), message); }
