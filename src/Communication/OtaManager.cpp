#include "OtaManager.h"
#include "../Core/Logger.h"
#include <ArduinoOTA.h>

namespace { bool gActive = false; }

void OtaManager::begin(const char *hostname) {
  if (hostname && hostname[0]) ArduinoOTA.setHostname(hostname);
  ArduinoOTA.onStart([]() { gActive = true; Logger::info(F("OTA update started")); });
  ArduinoOTA.onEnd([]() { gActive = false; Logger::info(F("OTA update completed")); });
  ArduinoOTA.onError([](ota_error_t error) {
    gActive = false;
    Logger::error(String(F("OTA error: ")) + static_cast<uint32_t>(error));
  });
  ArduinoOTA.begin();
  Logger::info(F("Arduino OTA ready"));
}

void OtaManager::update() { ArduinoOTA.handle(); }
bool OtaManager::active() { return gActive; }
