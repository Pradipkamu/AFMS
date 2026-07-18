#include "RuntimeStateManager.h"
#include "../Machine/ProductionManager.h"
#include "../Machine/RejectManager.h"
#include "../Machine/ShiftManager.h"
#include "../Machine/OEEManager.h"
#include "../Machine/MachineEngine.h"
#include "../Core/Logger.h"
#include <LittleFS.h>
#include <ArduinoJson.h>

namespace {
constexpr const char *kConfigPath = "/machine.json";
constexpr const char *kStatePath = "/runtime_state.json";
constexpr const char *kTempPath = "/runtime_state.tmp";
constexpr const char *kBackupPath = "/runtime_state.bak";
constexpr uint32_t kDefaultIntervalSeconds = 60;
constexpr uint32_t kMinIntervalSeconds = 10;
constexpr uint32_t kMaxIntervalSeconds = 3600;
constexpr uint32_t kDeferredSaveDelayMs = 1000UL;
bool gEnabled = true;
bool gRestoreOnBoot = true;
bool gRestored = false;
bool gSavePending = false;
uint32_t gIntervalSeconds = kDefaultIntervalSeconds;
uint32_t gLastSaveMs = 0;
uint32_t gSaveRequestedMs = 0;
uint32_t gSaveSuccess = 0;
uint32_t gSaveFailure = 0;
uint32_t gLastSavedChecksum = 0;

uint32_t mix(uint32_t value, uint32_t item) {
  value ^= item;
  return (value << 5) | (value >> 27);
}

uint32_t checksumV2(uint32_t production,
                    uint32_t reject,
                    const ShiftSnapshot &shift,
                    const OEEPersistentState &oee) {
  uint32_t value = 0xA5F5022AUL;
  value = mix(value, production);
  value = mix(value, reject);
  value = mix(value, shift.shiftId);
  value = mix(value, shift.operatorId);
  value = mix(value, shift.partNumber);
  value = mix(value, shift.targetQuantity);
  value = mix(value, shift.production);
  value = mix(value, shift.reject);
  value = mix(value, shift.startedAtEpoch);
  value = mix(value, oee.measuredElapsedMs);
  value = mix(value, oee.plannedShutdownMs);
  value = mix(value, oee.downtimeMs);
  value = mix(value, oee.productionBaseline);
  value = mix(value, oee.rejectBaseline);
  for (uint8_t i = 0; i < 17; ++i) value = mix(value, oee.lossSeconds[i]);
  for (size_t i = 0; i < sizeof(shift.partName); ++i) value = (value * 33UL) ^ static_cast<uint8_t>(shift.partName[i]);
  return value;
}

uint32_t checksumV3(uint32_t production,
                    uint32_t reject,
                    const ShiftSnapshot &shift,
                    const OEEPersistentState &oee,
                    uint16_t lastLossCode,
                    uint32_t lastLossDurationSeconds) {
  uint32_t value = checksumV2(production, reject, shift, oee);
  value = mix(value, lastLossCode);
  value = mix(value, lastLossDurationSeconds);
  return value;
}

void loadConfiguration() {
  File file = LittleFS.open(kConfigPath, "r");
  if (!file) return;
  StaticJsonDocument<1536> document;
  const DeserializationError error = deserializeJson(document, file);
  file.close();
  if (error) return;
  JsonObject runtime = document["runtime_state"];
  if (runtime.isNull()) gIntervalSeconds = document["runtime_state_save_interval_seconds"] | kDefaultIntervalSeconds;
  else {
    gEnabled = runtime["enabled"] | true;
    gRestoreOnBoot = runtime["restore_on_boot"] | true;
    gIntervalSeconds = runtime["save_interval_seconds"] | kDefaultIntervalSeconds;
  }
  if (gIntervalSeconds < kMinIntervalSeconds || gIntervalSeconds > kMaxIntervalSeconds) gIntervalSeconds = kDefaultIntervalSeconds;
}

bool restoreState() {
  File file = LittleFS.open(kStatePath, "r");
  if (!file) return false;
  DynamicJsonDocument document(4608);
  const DeserializationError error = deserializeJson(document, file);
  file.close();
  const uint16_t version = document["version"] | 0U;
  if (error || (version != 2U && version != 3U)) return false;

  const uint32_t production = document["production"] | 0UL;
  const uint32_t reject = document["reject"] | 0UL;
  ShiftSnapshot shift = {};
  shift.shiftId = document["shift_id"] | 1U;
  shift.operatorId = document["operator_id"] | 0UL;
  shift.partNumber = document["part_number"] | 0UL;
  strlcpy(shift.partName, document["part_name"] | "", sizeof(shift.partName));
  shift.targetQuantity = document["target_quantity"] | 0UL;
  shift.production = document["shift_production"] | 0UL;
  shift.reject = document["shift_reject"] | 0UL;
  shift.good = shift.production > shift.reject ? shift.production - shift.reject : 0;
  shift.startedAtEpoch = document["shift_started_at"] | 0UL;

  OEEPersistentState oee = {};
  oee.measuredElapsedMs = document["oee_measured_elapsed_ms"] | 0UL;
  oee.plannedShutdownMs = document["oee_planned_shutdown_ms"] | 0UL;
  oee.downtimeMs = document["oee_downtime_ms"] | 0UL;
  oee.productionBaseline = document["oee_production_baseline"] | 0UL;
  oee.rejectBaseline = document["oee_reject_baseline"] | 0UL;
  JsonArray losses = document["loss_seconds"].as<JsonArray>();
  for (uint8_t i = 0; i < 17; ++i) oee.lossSeconds[i] = losses.isNull() ? 0UL : (losses[i] | 0UL);

  const uint16_t lastLossCode = version >= 3U ? (document["last_loss_code"] | 0U) : 0U;
  const uint32_t lastLossDurationSeconds = version >= 3U ? (document["last_loss_duration_seconds"] | 0UL) : 0UL;
  const uint32_t storedChecksum = document["checksum"] | 0UL;
  const uint32_t expectedChecksum = version >= 3U
      ? checksumV3(production, reject, shift, oee, lastLossCode, lastLossDurationSeconds)
      : checksumV2(production, reject, shift, oee);
  if (storedChecksum != expectedChecksum) return false;

  ProductionManager::restore(production);
  RejectManager::restore(reject);
  ShiftManager::restoreRuntime(shift, production, reject);
  OEEManager::restorePersistentState(oee);
  OEEManager::setTargetQuantity(shift.targetQuantity);
  MachineEngine::restoreLastLoss(lastLossCode, lastLossDurationSeconds);
  gLastSavedChecksum = version >= 3U ? storedChecksum : 0;
  Logger::info(String(F("[STATE] Restored production=")) + production + F(", reject=") + reject);
  return true;
}
}

void RuntimeStateManager::begin() {
  loadConfiguration();
  if (!gEnabled) return;
  if (gRestoreOnBoot) gRestored = restoreState();
  gLastSaveMs = millis();
  Logger::info(String(F("[STATE] Save interval: ")) + gIntervalSeconds + F(" sec"));
}

void RuntimeStateManager::update() {
  if (!gEnabled) return;
  const uint32_t now = millis();
  if (gSavePending && static_cast<uint32_t>(now - gSaveRequestedMs) >= kDeferredSaveDelayMs) {
    if (saveNow()) gSavePending = false;
    gLastSaveMs = now;
    return;
  }

  const uint32_t intervalMs = gIntervalSeconds * 1000UL;
  if (static_cast<uint32_t>(now - gLastSaveMs) < intervalMs) return;
  gLastSaveMs = now;
  saveNow();
}

void RuntimeStateManager::scheduleSave() {
  if (!gEnabled) return;
  gSavePending = true;
  gSaveRequestedMs = millis();
}

bool RuntimeStateManager::saveNow() {
  if (!gEnabled) return false;
  const uint32_t production = ProductionManager::total();
  const uint32_t reject = RejectManager::total();
  const ShiftSnapshot shift = ShiftManager::snapshot();
  const OEEPersistentState oee = OEEManager::persistentState();
  const uint16_t lastLossCode = MachineEngine::lastAcceptedLossCode();
  const uint32_t lastLossDurationSeconds = MachineEngine::lastLossDurationSeconds();
  const uint32_t currentChecksum = checksumV3(production, reject, shift, oee, lastLossCode, lastLossDurationSeconds);
  if (currentChecksum == gLastSavedChecksum) return true;

  DynamicJsonDocument document(4608);
  document["version"] = 3;
  document["production"] = production;
  document["reject"] = reject;
  document["shift_id"] = shift.shiftId;
  document["operator_id"] = shift.operatorId;
  document["part_number"] = shift.partNumber;
  document["part_name"] = shift.partName;
  document["target_quantity"] = shift.targetQuantity;
  document["shift_production"] = shift.production;
  document["shift_reject"] = shift.reject;
  document["shift_started_at"] = shift.startedAtEpoch;
  document["oee_measured_elapsed_ms"] = oee.measuredElapsedMs;
  document["oee_planned_shutdown_ms"] = oee.plannedShutdownMs;
  document["oee_downtime_ms"] = oee.downtimeMs;
  document["oee_production_baseline"] = oee.productionBaseline;
  document["oee_reject_baseline"] = oee.rejectBaseline;
  JsonArray losses = document.createNestedArray("loss_seconds");
  for (uint8_t i = 0; i < 17; ++i) losses.add(oee.lossSeconds[i]);
  document["last_loss_code"] = lastLossCode;
  document["last_loss_duration_seconds"] = lastLossDurationSeconds;
  document["checksum"] = currentChecksum;

  File file = LittleFS.open(kTempPath, "w");
  if (!file) { ++gSaveFailure; return false; }
  const size_t written = serializeJson(document, file);
  file.flush();
  file.close();
  if (written == 0) { LittleFS.remove(kTempPath); ++gSaveFailure; return false; }

  LittleFS.remove(kBackupPath);
  if (LittleFS.exists(kStatePath) && !LittleFS.rename(kStatePath, kBackupPath)) {
    LittleFS.remove(kTempPath); ++gSaveFailure; return false;
  }
  if (!LittleFS.rename(kTempPath, kStatePath)) {
    if (LittleFS.exists(kBackupPath)) LittleFS.rename(kBackupPath, kStatePath);
    LittleFS.remove(kTempPath); ++gSaveFailure; return false;
  }
  LittleFS.remove(kBackupPath);
  gLastSavedChecksum = currentChecksum;
  ++gSaveSuccess;
  Logger::info(String(F("[STATE] Saved production=")) + production + F(", reject=") + reject);
  return true;
}

bool RuntimeStateManager::restored() { return gRestored; }
uint32_t RuntimeStateManager::saveIntervalSeconds() { return gIntervalSeconds; }
uint32_t RuntimeStateManager::saveSuccessCount() { return gSaveSuccess; }
uint32_t RuntimeStateManager::saveFailureCount() { return gSaveFailure; }
