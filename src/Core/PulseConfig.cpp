#include "PulseConfig.h"
#include "Logger.h"
#include <LittleFS.h>
#include <ArduinoJson.h>

namespace {
uint32_t gProductionMs = 5;
uint32_t gRejectMs = 5;
uint32_t bounded(JsonVariantConst value,uint32_t fallback){const uint32_t parsed=value|fallback;return parsed>=1&&parsed<=60000?parsed:fallback;}
}

void PulseConfig::load() {
  File file = LittleFS.open("/device.json", "r");
  if (!file) {
    Logger::warn(F("[PULSE] device.json missing; using 5 ms debounce"));
    return;
  }
  DynamicJsonDocument doc(4096);
  const auto error=deserializeJson(doc,file);
  file.close();
  if(error){Logger::warn(F("[PULSE] invalid device.json; using active debounce"));return;}
  gProductionMs=bounded(doc["production_pulse_debounce_ms"],5);
  gRejectMs=bounded(doc["reject_pulse_debounce_ms"],5);
  Logger::info(String(F("[PULSE] Production debounce: ")) + gProductionMs + F(" ms"));
  Logger::info(String(F("[PULSE] Reject debounce: ")) + gRejectMs + F(" ms"));
}

uint32_t PulseConfig::productionDebounceMs() { return gProductionMs; }
uint32_t PulseConfig::rejectDebounceMs() { return gRejectMs; }
