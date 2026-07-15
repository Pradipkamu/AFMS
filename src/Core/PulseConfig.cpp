#include "PulseConfig.h"
#include "Logger.h"
#include <LittleFS.h>

namespace {
uint32_t gProductionMs = 5;
uint32_t gRejectMs = 5;

uint32_t readJsonUint(const String &json, const char *key, uint32_t fallback) {
  const String token = String('"') + key + "\"";
  const int keyPos = json.indexOf(token);
  if (keyPos < 0) return fallback;
  const int colon = json.indexOf(':', keyPos + token.length());
  if (colon < 0) return fallback;
  int start = colon + 1;
  while (start < static_cast<int>(json.length()) && isspace(static_cast<unsigned char>(json[start]))) ++start;
  int end = start;
  while (end < static_cast<int>(json.length()) && isDigit(json[end])) ++end;
  if (end == start) return fallback;
  const uint32_t value = static_cast<uint32_t>(json.substring(start, end).toInt());
  return value >= 1 && value <= 60000 ? value : fallback;
}
}

void PulseConfig::load() {
  File file = LittleFS.open("/machine.json", "r");
  if (!file) {
    Logger::warn(F("[PULSE] machine.json missing; using 5 ms debounce"));
    return;
  }
  const String json = file.readString();
  file.close();
  gProductionMs = readJsonUint(json, "production_pulse_debounce_ms", 5);
  gRejectMs = readJsonUint(json, "reject_pulse_debounce_ms", 5);
  Logger::info(String(F("[PULSE] Production debounce: ")) + gProductionMs + F(" ms"));
  Logger::info(String(F("[PULSE] Reject debounce: ")) + gRejectMs + F(" ms"));
}

uint32_t PulseConfig::productionDebounceMs() { return gProductionMs; }
uint32_t PulseConfig::rejectDebounceMs() { return gRejectMs; }