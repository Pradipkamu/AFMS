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

void completeCurrentShift(bool archiveCsv) {
  refreshCounters();
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
  gCompletedSummary += F(",\"operator_id\":");
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
}

uint16_t scheduledShiftId() {
  const time_t now = TimeManager::now();
  struct tm local;
  if (!localtime_r(&now, &local)) return gShift.shiftId;
  if (local.tm_hour >= 6 && local.tm_hour < 14) return 1;
  if (local.tm_hour >= 14 && local.tm_hour < 22) return 2;
  return 3;
}
}

void ShiftManager::begin() {
  copyName("");
  ShiftCsvManager::begin();
  resetBaselines();
}

void ShiftManager::update() {
  refreshCounters();
  if (!TimeManager::synchronized() || millis() - gLastScheduleCheckMs < 1000UL) return;
  gLastScheduleCheckMs = millis();
  const uint16_t expected = scheduledShiftId();

  if (!gScheduleInitialized) {
    gScheduleInitialized = true;
    gShift.shiftId = expected;
    resetBaselines();
    Logger::info(String(F("[SHIFT] Active shift: ")) + expected);
    return;
  }

  if (expected != gShift.shiftId) setShift(expected);
}

void ShiftManager::setShift(uint16_t shiftId) {
  if (shiftId == 0 || shiftId > 3 || shiftId == gShift.shiftId) return;
  completeCurrentShift(true);
  gShift.shiftId = shiftId;
  resetBaselines();
  Logger::info(String(F("[SHIFT] Changed to shift: ")) + shiftId);
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

ShiftSnapshot ShiftManager::snapshot() {
  refreshCounters();
  return gShift;
}

bool ShiftManager::consumeCompletedSummary(String &json) {
  if (gCompletedSummary.length() == 0) return false;
  json = gCompletedSummary;
  gCompletedSummary = "";
  return true;
}
