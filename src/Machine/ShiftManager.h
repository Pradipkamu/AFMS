#pragma once
#include <Arduino.h>

struct ShiftSnapshot {
  uint16_t shiftId;
  uint32_t operatorId;
  uint32_t partNumber;
  char partName[17];
  uint32_t targetQuantity;
  uint32_t production;
  uint32_t reject;
  uint32_t good;
  uint32_t startedAtEpoch;

  // HMI/OEE compatibility fields. These are populated from the active OEE
  // schedule in newer builds; older runtime state remains valid because
  // aggregate initialization zero-fills these appended members.
  uint32_t scheduledElapsedSeconds;
  uint32_t plannedShutdownSeconds;
  uint32_t plannedProductionSeconds;
};

namespace ShiftManager {
void begin();
void update();
void setShift(uint16_t shiftId);
void setOperatorId(uint32_t operatorId);
void setPart(uint32_t partNumber, const char *partName);
void setTargetQuantity(uint32_t targetQuantity);
void restoreRuntime(const ShiftSnapshot &state, uint32_t totalProduction, uint32_t totalReject);
ShiftSnapshot snapshot();
bool consumeCompletedSummary(String &json);
}