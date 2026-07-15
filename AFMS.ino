#include "src/Core/Version.h"
#include "src/Core/Logger.h"
#include "src/Core/Config.h"
#include "src/Core/EventBus.h"
#include "src/Core/SystemHealth.h"
#include "src/Core/ReliabilityManager.h"
#include "src/Core/SerialDiagnostics.h"
#include "src/Storage/LittleFSManager.h"
#include "src/Communication/WiFiManager.h"
#include "src/Communication/CloudManager.h"
#include "src/Communication/OtaManager.h"
#include "src/Communication/WebManager.h"
#include "src/Machine/MachineEngine.h"
#include "src/Machine/ShiftManager.h"
#include "src/HMI/HMIManager.h"

void setup() {
  Serial.begin(115200);
  delay(10);

  Logger::begin(Serial);
  Logger::info(F("Booting AFMS " AFMS_VERSION));

  if (!LittleFSManager::begin()) {
    Logger::error(F("LittleFS mount failed"));
  }

  ReliabilityManager::begin();
  Config::load();
  EventBus::begin();
  SystemHealth::begin();
  WiFiManager::begin(Config::wifiSsid(), Config::wifiPassword());
  MachineEngine::begin();
  CloudManager::begin();
  ShiftManager::begin();
  HMIManager::begin();
  WebManager::begin();
  OtaManager::begin(Config::machineId());
  SerialDiagnostics::begin();
}

void loop() {
  ReliabilityManager::update();
  WiFiManager::update();
  MachineEngine::update();
  ShiftManager::update();
  HMIManager::update();
  if (!ReliabilityManager::safeMode()) CloudManager::update();
  WebManager::update();
  OtaManager::update();
  SerialDiagnostics::update();
  EventBus::update();
  yield();
}
