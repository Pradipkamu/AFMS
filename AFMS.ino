#include "src/Core/Version.h"
#include "src/Core/Logger.h"
#include "src/Core/Config.h"
#include "src/Core/LossCatalog.h"
#include "src/Core/EventBus.h"
#include "src/Core/SystemHealth.h"
#include "src/Core/ReliabilityManager.h"
#include "src/Core/SerialDiagnostics.h"
#include "src/Core/HardwareConfig.h"
#include "src/Storage/LittleFSManager.h"
#include "src/Storage/RuntimeStateManager.h"
#include "src/Storage/OfflineQueue.h"
#include "src/Communication/WiFiManager.h"
#include "src/Communication/CloudManager.h"
#include "src/Communication/CommunicationManager.h"
#include "src/Communication/AfmsWebClient.h"
#include "src/Communication/RemoteConfigManager.h"
#include "src/Communication/ReconnectManager.h"
#include "src/Communication/OtaManager.h"
#include "src/Communication/WebManager.h"
#include "src/Machine/MachineEngine.h"
#include "src/Machine/ShiftManager.h"
#include "src/HMI/HMIManager.h"

void setup() {
  Serial1.begin(HardwareConfig::DiagnosticBaud);
  delay(10);
  Logger::begin(Serial1);
  Logger::info(F("Booting AFMS " AFMS_VERSION));
  if (!LittleFSManager::begin()) Logger::error(F("LittleFS mount failed"));
  OfflineQueue::begin();
  ReliabilityManager::begin();
  Config::load();
  LossCatalog::begin();
  EventBus::begin();
  SystemHealth::begin();
  WiFiManager::begin(Config::wifiSsid(), Config::wifiPassword());
  MachineEngine::begin();
  CloudManager::begin();
  CommunicationManager::begin();
  AfmsWebClient::begin();
  RemoteConfigManager::begin();
  ReconnectManager::begin();
  ShiftManager::begin();
  RuntimeStateManager::begin();
  HMIManager::begin();
  WebManager::begin();
  OtaManager::begin(Config::machineId());
  SerialDiagnostics::begin();
}

void loop() {
  ReliabilityManager::update();
  MachineEngine::update();
  HMIManager::update();
  ShiftManager::update();
  RuntimeStateManager::update();
  WiFiManager::update();
  ReconnectManager::update();
  RemoteConfigManager::update();
  CommunicationManager::update();
  if (!ReliabilityManager::safeMode()) {
    AfmsWebClient::update();
    CloudManager::update();
  }
  WebManager::update();
  OtaManager::update();
  SerialDiagnostics::update();
  EventBus::update();
  yield();
}
