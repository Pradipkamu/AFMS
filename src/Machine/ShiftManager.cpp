#include "ShiftManager.h"
#include "MachineEngine.h"
#include "OEEManager.h"
#include "../Communication/TimeManager.h"
#include "../Core/Config.h"
#include <cstring>

namespace {
ShiftSnapshot gShift = {1, 0, 0, "", 0, 0, 0, 0, 0};
uint32_t gBaseProduction = 0;
uint32_t gBaseReject = 0;
String gCompletedSummary;

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

void completeCurrentShift() {
  refreshCounters();
  const OEESnapshot oee = OEEManager::snapshot();
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
}

void ShiftManager::begin() {
  copyName("");
  resetBaselines();
}

void ShiftManager::update() { refreshCounters(); }

void ShiftManager::setShift(uint16_t shiftId) {
  if (shiftId == 0 || shiftId == gShift.shiftId) return;
  completeCurrentShift();
  gShift.shiftId = shiftId;
  resetBaselines();
}

void ShiftManager::setOperatorId(uint32_t operatorId) { gShift.operatorId = operatorId; }

void ShiftManager::setPart(uint32_t partNumber, const char *partName) {
  if (partNumber == gShift.partNumber && strncmp(gShift.partName, partName ? partName : "", sizeof(gShift.partName)) == 0) return;
  completeCurrentShift();
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
