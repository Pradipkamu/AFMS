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
#include "src/Communication/WiFiManager.h"
#include "src/Communication/CloudManager.h"
#include "src/Communication/ReconnectManager.h"
#include "src/Communication/OtaManager.h"
#include "src/Communication/WebManager.h"
#include "src/Machine/MachineEngine.h"
#include "src/Machine/ShiftManager.h"
#include "src/HMI/HMIManager.h"
#include "src/Reporting/ReportOutboxManager.h"
#include "src/Reporting/LegacyReportingMigration.h"
#include "src/Reporting/MonthRolloverManager.h"

namespace {
uint32_t gLastReportingCleanupMs = 0;
constexpr uint32_t kReportingCleanupIntervalMs = 60000UL;

void initializeReportingStorage() {
  ReportOutboxManager::begin();
  const LegacyReportingMigration::Result migration = LegacyReportingMigration::run();

  if (migration.googleRecordsImported || migration.hourlyRecordsImported) {
    Logger::info(String(F("[REPORT] Migrated legacy Google records: ")) +
                 migration.googleRecordsImported +
                 F(", hourly records: ") + migration.hourlyRecordsImported);
  }
  if (migration.invalidRecordsSkipped) {
    Logger::warn(String(F("[REPORT] Invalid legacy records skipped: ")) +
                 migration.invalidRecordsSkipped);
  }
  if (!migration.googleQueueCompleted || !migration.hourlyQueueCompleted) {
    Logger::warn(F("[REPORT] Legacy migration incomplete; source files retained"));
  }

  LegacyReportingMigration::cleanupMigrationBackups();
  MonthRolloverManager::begin();
}

void updateReportingMaintenance() {
  MonthRolloverManager::update(TimeManager::synchronized(), TimeManager::now());
  const uint32_t nowMs = millis();
  if (nowMs - gLastReportingCleanupMs >= kReportingCleanupIntervalMs) {
    gLastReportingCleanupMs = nowMs;
    LegacyReportingMigration::cleanupMigrationBackups();
  }
}
}

void setup() {
  Serial1.begin(HardwareConfig::DiagnosticBaud);
  delay(10);

  Logger::begin(Serial1);
  Logger::info(F("Booting AFMS " AFMS_VERSION));

  if (!LittleFSManager::begin()) {
    Logger::error(F("LittleFS mount failed"));
  }

  initializeReportingStorage();
  ReliabilityManager::begin();
  Config::load();
  LossCatalog::begin();
  EventBus::begin();
  SystemHealth::begin();
  WiFiManager::begin(Config::wifiSsid(), Config::wifiPassword());
  MachineEngine::begin();
  CloudManager::begin();
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
  WiFiManager::update();
  ReconnectManager::update();
  MachineEngine::update();
  ShiftManager::update();
  RuntimeStateManager::update();
  HMIManager::update();
  if (!ReliabilityManager::safeMode()) {
    CloudManager::update();
  }
  updateReportingMaintenance();
  WebManager::update();
  OtaManager::update();
  SerialDiagnostics::update();
  EventBus::update();
  yield();
}
