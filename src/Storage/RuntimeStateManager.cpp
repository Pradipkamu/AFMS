#include "RuntimeStateManager.h"
#include "../Machine/ProductionManager.h"
#include "../Machine/RejectManager.h"
#include "../Machine/ShiftManager.h"
#include "../Core/Logger.h"
#include <LittleFS.h>
#include <ArduinoJson.h>

namespace {
constexpr const char *kConfigPath = "/machine.json";
constexpr const char *kStatePath = "/runtime_state.json";
constexpr const char *kTempPath = "/runtime_state.tmp";
constexpr uint32_t kDefaultIntervalSeconds = 60;
constexpr uint32_t kMinIntervalSeconds = 10;
constexpr uint32_t kMaxIntervalSeconds = 3600;

bool gEnabled = true;
bool gRestoreOnBoot = true;
bool gRestored = false;
uint32_t gIntervalSeconds = kDefaultIntervalSeconds;
uint32_t gLastSaveMs = 0;
uint32_t gSaveSuccess = 0;
uint32_t gSaveFailure = 0;

uint32_t checksum(uint32_t production,
                  uint32_t reject,
                  const ShiftSnapshot &shift) {
  uint32_t value = 0xA5F5022AUL;
  value ^= production;
  value = (value << 5) | (value >> 27);
  value ^= reject;
  value = (value << 5) | (value >> 27);
  value ^= shift.shiftId;
  value ^= shift.operatorId;
  value ^= shift.partNumber;
  value ^= shift.targetQuantity;
  value ^= shift.production;
  value ^= shift.reject;
  value ^= shift.startedAtEpoch;
  for (size_t i = 0; i < sizeof(shift.partName); ++i) {
    value = (value * 33UL) ^ static_cast<uint8_t>(shift.partName[i]);
  }
  return value;
}

void loadConfiguration() {
  File file = LittleFS.open(kConfigPath, "r");
  if (!file) {
    Logger::warn(F("[STATE] machine.json unavailable; using 60-second save interval"));
    return;
  }

  StaticJsonDocument<1536> document;
  const DeserializationError error = deserializeJson(document, file);
  file.close();
  if (error) {
    Logger::warn(F("[STATE] Runtime configuration invalid; using defaults"));
    return;
  }

  JsonObject runtime = document["runtime_state"];
  if (runtime.isNull()) {
    gIntervalSeconds = document["runtime_state_save_interval_seconds"] | kDefaultIntervalSeconds;
  } else {
    gEnabled = runtime["enabled"] | true;
    gRestoreOnBoot = runtime["restore_on_boot"] | true;
    gIntervalSeconds = runtime["save_interval_seconds"] | kDefaultIntervalSeconds;
  }

  if (gIntervalSeconds < kMinIntervalSeconds || gIntervalSeconds > kMaxIntervalSeconds) {
    Logger::warn(F("[STATE] Save interval out of range; using 60 seconds"));
    gIntervalSeconds = kDefaultIntervalSeconds;
  }
}

bool restoreState() {
  File file = LittleFS.open(kStatePath, "r");
  if (!file) return false;

  StaticJsonDocument<1024> document;
  const DeserializationError error = deserializeJson(document, file);
  file.close();
  if (error || document["version"].as<uint16_t>() != 1U) {
    Logger::warn(F("[STATE] Saved runtime state invalid"));
    return false;
  }

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

  const uint32_t storedChecksum = document["checksum"] | 0UL;
  if (storedChecksum != checksum(production, reject, shift)) {
    Logger::warn(F("[STATE] Runtime checksum mismatch; state ignored"));
    return false;
  }

  ProductionManager::restore(production);
  RejectManager::restore(reject);
  ShiftManager::restoreRuntime(shift, production, reject);
  Logger::info(String(F("[STATE] Restored production=")) + production +
               F(", reject=") + reject + F(", shift=") + shift.shiftId);
  return true;
}
}

void RuntimeStateManager::begin() {
  loadConfiguration();
  if (!gEnabled) {
    Logger::warn(F("[STATE] Runtime persistence disabled"));
    return;
  }

  if (gRestoreOnBoot) gRestored = restoreState();
  gLastSaveMs = millis();
  Logger::info(String(F("[STATE] Save interval: ")) + gIntervalSeconds + F(" sec"));
}

void RuntimeStateManager::update() {
  if (!gEnabled) return;
  const uint32_t intervalMs = gIntervalSeconds * 1000UL;
  if (static_cast<uint32_t>(millis() - gLastSaveMs) < intervalMs) return;
  gLastSaveMs = millis();
  saveNow();
}

bool RuntimeStateManager::saveNow() {
  if (!gEnabled) return false;

  const uint32_t production = ProductionManager::total();
  const uint32_t reject = RejectManager::total();
  const ShiftSnapshot shift = ShiftManager::snapshot();

  StaticJsonDocument<1024> document;
  document["version"] = 1;
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
  document["checksum"] = checksum(production, reject, shift);

  File file = LittleFS.open(kTempPath, "w");
  if (!file) {
    ++gSaveFailure;
    Logger::warn(F("[STATE] Unable to open temporary state file"));
    return false;
  }

  const size_t written = serializeJson(document, file);
  file.flush();
  file.close();
  if (written == 0) {
    LittleFS.remove(kTempPath);
    ++gSaveFailure;
    Logger::warn(F("[STATE] Runtime state write failed"));
    return false;
  }

  LittleFS.remove(kStatePath);
  if (!LittleFS.rename(kTempPath, kStatePath)) {
    LittleFS.remove(kTempPath);
    ++gSaveFailure;
    Logger::warn(F("[STATE] Runtime state commit failed"));
    return false;
  }

  ++gSaveSuccess;
  Logger::info(String(F("[STATE] Saved production=")) + production + F(", reject=") + reject);
  return true;
}

bool RuntimeStateManager::restored() { return gRestored; }
uint32_t RuntimeStateManager::saveIntervalSeconds() { return gIntervalSeconds; }
uint32_t RuntimeStateManager::saveSuccessCount() { return gSaveSuccess; }
uint32_t RuntimeStateManager::saveFailureCount() { return gSaveFailure; }
