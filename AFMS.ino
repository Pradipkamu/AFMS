#include "src/Core/Version.h"
#include "src/Core/Logger.h"
#include "src/Core/Config.h"
#include "src/Core/EventBus.h"
#include "src/Storage/LittleFSManager.h"
#include "src/Communication/WiFiManager.h"
#include "src/Machine/MachineEngine.h"

void setup() {
  Serial.begin(115200);
  delay(10);

  Logger::begin(Serial);
  Logger::info(F("Booting AFMS " AFMS_VERSION));

  if (!LittleFSManager::begin()) {
    Logger::error(F("LittleFS mount failed"));
  }

  Config::load();
  EventBus::begin();
  WiFiManager::begin(Config::wifiSsid(), Config::wifiPassword());
  MachineEngine::begin();
}

void loop() {
  WiFiManager::update();
  MachineEngine::update();
  EventBus::update();
  yield();
}
