#include "ShiftManager.h"
#include "MachineEngine.h"
#include "OEEManager.h"
#include "../Communication/TimeManager.h"
#include "../Core/Config.h"
#include "../Core/Logger.h"
#include "../Storage/ShiftCsvManager.h"
#include <cstring>
#include <time.h>

namespace {
ShiftSnapshot gShift = {1, 0, 0, "", 0, 0, 0, 0, 0};
uint32_t gBaseProduction = 0;
uint32_t gBaseReject = 0;
String gCompletedSummary;
bool gScheduleInitialized = false;
uint32_t gLastScheduleCheckMs = 0;

String jsonEscape(const char *value) {
  String out;
  if (!value) return out;
  while (*value) {
    if (*value == '"' || *value == '\\') out += '\\';
    out += *value++;
  }
  return out;
}

void copyName(const char *name) {
  if (!name) name = "";
  strncpy(gShift.partName, name, sizeof(gShift.partName) - 1);
  gShift.partName[sizeof(gShift.partName) - 1] = '\0';
}

void refreshCounters() {
  const MachineSnapshot machine = MachineEngine::snapshot();
  gShift.production = machine.totalParts - gBaseProduction;
  gShift.reject = machine.rejectParts - gBaseReject;
  gShift.good = gShift.production > gShift.reject ? gShift.production - gShift.reject : 0;
}

bool minuteInRange(uint16_t minute, uint16_t start, uint16_t end) {
  return start < end ? (minute >= start && minute < end)
                     : (minute >= start || minute < end);
}

uint16_t scheduledShiftId() {
  const time_t now = TimeManager::now();
  struct tm local;
  if (!localtime_r(&now, &local)) return 0;
  const uint16_t minute = static_cast<uint16_t>(local.tm_hour * 60U + local.tm_min);
  for (uint8_t i = 0; i < 3; ++i) {
    if (minuteInRange(minute, Config::shiftStartMinutes(i), Config::shiftEndMinutes(i))) return i + 1;
  }
  return 0;
}

uint32_t scheduledElapsedSeconds(uint16_t shiftId) {
  if (shiftId < 1 || shiftId > 3 || !TimeManager::synchronized()) return 0;
  const time_t now = TimeManager::now();
  struct tm local;
  if (!localtime_r(&now, &local)) return 0;

  const uint16_t startMinute = Config::shiftStartMinutes(shiftId - 1);
  const uint16_t endMinute = Config::shiftEndMinutes(shiftId - 1);
  const uint16_t currentMinute = static_cast<uint16_t>(local.tm_hour * 60U + local.tm_min);
  int32_t elapsedMinutes = 0;

  if (startMinute < endMinute) {
    elapsedMinutes = static_cast<int32_t>(currentMinute) - startMinute;
  } else {
    elapsedMinutes = currentMinute >= startMinute
                         ? static_cast<int32_t>(currentMinute) - startMinute
                         : static_cast<int32_t>(currentMinute) + 1440 - startMinute;
  }

  if (elapsedMinutes < 0) elapsedMinutes = 0;
  return static_cast<uint32_t>(elapsedMinutes) * 60UL + static_cast<uint32_t>(local.tm_sec);
}

void syncOeeScheduleTime() {
  if (!Config::shiftScheduleValid() || !TimeManager::synchronized()) return;
  OEEManager::setScheduledShiftElapsedSeconds(scheduledElapsedSeconds(gShift.shiftId));
}

void completeCurrentShift(bool archiveCsv) {
  refreshCounters();
  syncOeeScheduleTime();
  const OEESnapshot oee = OEEManager::snapshot();
  const uint32_t endedAtEpoch = static_cast<uint32_t>(TimeManager::now());

  gCompletedSummary = F("{\"record_type\":\"shift_summary\",\"api_token\":\"");
  gCompletedSummary += jsonEscape(Config::apiToken());
  gCompletedSummary += F("\",\"machine_id\":\"");
  gCompletedSummary += jsonEscape(Config::machineId());
  gCompletedSummary += F("\",\"machine_name\":\"");
  gCompletedSummary += jsonEscape(Config::machineName());
  gCompletedSummary += F("\",\"timestamp\":\"");
  gCompletedSummary += TimeManager::iso8601();
  gCompletedSummary += F("\",\"shift\":");
  gCompletedSummary += gShift.shiftId;
  gCompletedSummary += F(",\"shift_name\":\"");
  gCompletedSummary += jsonEscape(Config::shiftName(gShift.shiftId - 1));
  gCompletedSummary += F("\",\"operator_id\":");
  gCompletedSummary += gShift.operatorId;
  gCompletedSummary += F(",\"part_number\":");
  gCompletedSummary += gShift.partNumber;
  gCompletedSummary += F(",\"part_name\":\"");
  gCompletedSummary += jsonEscape(gShift.partName);
  gCompletedSummary += F("\",\"target\":");
  gCompletedSummary += gShift.targetQuantity;
  gCompletedSummary += F(",\"production\":");
  gCompletedSummary += gShift.production;
  gCompletedSummary += F(",\"reject\":");
  gCompletedSummary += gShift.reject;
  gCompletedSummary += F(",\"good\":");
  gCompletedSummary += gShift.good;
  gCompletedSummary += F(",\"availability_permille\":");
  gCompletedSummary += oee.availabilityPermille;
  gCompletedSummary += F(",\"performance_permille\":");
  gCompletedSummary += oee.performancePermille;
  gCompletedSummary += F(",\"quality_permille\":");
  gCompletedSummary += oee.qualityPermille;
  gCompletedSummary += F(",\"oee_permille\":");
  gCompletedSummary += oee.oeePermille;
  gCompletedSummary += F("}");

  if (archiveCsv) ShiftCsvManager::appendShift(gShift, oee, endedAtEpoch);
}

void resetBaselines() {
  const MachineSnapshot machine = MachineEngine::snapshot();
  gBaseProduction = machine.totalParts;
  gBaseReject = machine.rejectParts;
  gShift.production = gShift.reject = gShift.good = 0;
  gShift.startedAtEpoch = static_cast<uint32_t>(TimeManager::now());
  OEEManager::resetShift();
  OEEManager::setTargetQuantity(gShift.targetQuantity);
  syncOeeScheduleTime();
}
}

void ShiftManager::begin() {
  copyName("");
  ShiftCsvManager::begin();
  resetBaselines();
  if (!Config::shiftScheduleValid()) Logger::error(F("[SHIFT] Automatic shift management disabled"));
}

void ShiftManager::update() {
  refreshCounters();
  syncOeeScheduleTime();
  if (!Config::shiftScheduleValid() || !TimeManager::synchronized() || millis() - gLastScheduleCheckMs < 1000UL) return;
  gLastScheduleCheckMs = millis();
  const uint16_t expected = scheduledShiftId();
  if (expected == 0) {
    Logger::error(F("[SHIFT] Current time does not match a configured shift"));
    return;
  }

  if (!gScheduleInitialized) {
    gScheduleInitialized = true;
    gShift.shiftId = expected;
    resetBaselines();
    Logger::info(String(F("[SHIFT] Active shift: ")) + Config::shiftName(expected - 1));
    return;
  }

  if (expected != gShift.shiftId) setShift(expected);
}

void ShiftManager::setShift(uint16_t shiftId) {
  if (shiftId == 0 || shiftId > 3 || shiftId == gShift.shiftId) return;
  completeCurrentShift(true);
  gShift.shiftId = shiftId;
  resetBaselines();
  Logger::info(String(F("[SHIFT] Changed to shift: ")) + Config::shiftName(shiftId - 1));
}

void ShiftManager::setOperatorId(uint32_t operatorId) { gShift.operatorId = operatorId; }

void ShiftManager::setPart(uint32_t partNumber, const char *partName) {
  if (partNumber == gShift.partNumber && strncmp(gShift.partName, partName ? partName : "", sizeof(gShift.partName)) == 0) return;
  completeCurrentShift(false);
  gShift.partNumber = partNumber;
  copyName(partName);
  resetBaselines();
}

void ShiftManager::setTargetQuantity(uint32_t targetQuantity) {
  gShift.targetQuantity = targetQuantity;
  OEEManager::setTargetQuantity(targetQuantity);
}

void ShiftManager::restoreRuntime(const ShiftSnapshot &state, uint32_t totalProduction, uint32_t totalReject) {
  gShift = state;
  copyName(state.partName);
  gBaseProduction = totalProduction >= state.production ? totalProduction - state.production : 0;
  gBaseReject = totalReject >= state.reject ? totalReject - state.reject : 0;
  gScheduleInitialized = true;
  OEEManager::setTargetQuantity(gShift.targetQuantity);
  syncOeeScheduleTime();
}

ShiftSnapshot ShiftManager::snapshot() {
  refreshCounters();
  syncOeeScheduleTime();
  return gShift;
}

bool ShiftManager::consumeCompletedSummary(String &json) {
  if (gCompletedSummary.length() == 0) return false;
  json = gCompletedSummary;
  gCompletedSummary = "";
  return true;
}