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
