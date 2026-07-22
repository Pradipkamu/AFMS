#include "Config.h"
#include "Logger.h"
#include <ArduinoJson.h>
#include <LittleFS.h>

namespace {
bool gLoaded = false;
bool gEnabled = false;
bool gActiveHigh = true;
uint16_t gDebounceMs = 50;
uint16_t gMinimumMs = 50;
uint32_t gTimeoutSeconds = 180;

void loadCycleEndSettings() {
  if (gLoaded) return;
  gLoaded = true;

  File file = LittleFS.open("/machine.json", "r");
  if (!file) {
    Logger::info(F("[CYCLE END] Configuration absent; fixed-time mode active"));
    return;
  }

  DynamicJsonDocument document(6144);
  const DeserializationError error = deserializeJson(document, file);
  file.close();
  if (error) {
    Logger::warn(F("[CYCLE END] Invalid machine.json; fixed-time mode active"));
    return;
  }

  gEnabled = document["cycle_end_enabled"] | false;
  gActiveHigh = document["cycle_end_active_high"] | true;

  const uint32_t debounce = document["cycle_end_debounce_ms"] | 50UL;
  gDebounceMs = static_cast<uint16_t>((debounce >= 1UL && debounce <= 1000UL) ? debounce : 50UL);

  const uint32_t minimum = document["cycle_end_minimum_ms"] | 50UL;
  gMinimumMs = static_cast<uint16_t>((minimum >= 1UL && minimum <= 60000UL) ? minimum : 50UL);

  const uint32_t timeout = document["cycle_end_timeout_seconds"] | 180UL;
  gTimeoutSeconds = (timeout >= 1UL && timeout <= 86400UL) ? timeout : 180UL;

  Logger::info(String(F("[CYCLE END] Mode ")) + (gEnabled ? F("enabled") : F("disabled")));
  if (gEnabled) {
    Logger::info(String(F("[CYCLE END] Timeout: ")) + gTimeoutSeconds + F(" sec"));
    Logger::info(String(F("[CYCLE END] Minimum valid interval: ")) + gMinimumMs + F(" ms"));
  }
}
}

bool Config::cycleEndEnabled() {
  loadCycleEndSettings();
  return gEnabled;
}

bool Config::cycleEndActiveHigh() {
  loadCycleEndSettings();
  return gActiveHigh;
}

uint16_t Config::cycleEndDebounceMs() {
  loadCycleEndSettings();
  return gDebounceMs;
}

uint16_t Config::cycleEndMinimumMs() {
  loadCycleEndSettings();
  return gMinimumMs;
}

uint32_t Config::cycleEndTimeoutSeconds() {
  loadCycleEndSettings();
  return gTimeoutSeconds;
}
