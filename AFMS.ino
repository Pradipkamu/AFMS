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
#include "src/Communication/NetworkBudget.h"
#include "src/Machine/MachineEngine.h"
#include "src/Machine/ShiftManager.h"
#include "src/HMI/HMIManager.h"

namespace {
uint32_t gMaxLoopUs = 0;
uint32_t gLastLoopReportMs = 0;
}

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
  const uint32_t loopStartedUs = micros();
  NetworkBudget::beginLoop();

  // Real-time machine work always runs before storage, UI and networking.
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
    // Fresh AFMS status/loss telemetry gets first access to the network budget.
    AfmsWebClient::update();
    CloudManager::update();
  }
  WebManager::update();
  OtaManager::update();
  SerialDiagnostics::update();
  EventBus::update();

  const uint32_t elapsedUs = micros() - loopStartedUs;
  if (elapsedUs > gMaxLoopUs) gMaxLoopUs = elapsedUs;
  if (millis() - gLastLoopReportMs >= 60000UL) {
    gLastLoopReportMs = millis();
    Logger::info(String(F("[PERF] Max loop: ")) + gMaxLoopUs + F(" us; deferred network: ") + NetworkBudget::deferredCount());
    gMaxLoopUs = 0;
  }
  yield();
}
