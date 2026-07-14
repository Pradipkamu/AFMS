#pragma once
#include <Arduino.h>

struct OEESnapshot {
  uint32_t plannedSeconds;
  uint32_t runSeconds;
  uint32_t downtimeSeconds;
  uint32_t targetQuantity;
  uint16_t availabilityPermille;
  uint16_t performancePermille;
  uint16_t qualityPermille;
  uint16_t oeePermille;
};

namespace OEEManager {
void begin(uint32_t idealCycleTimeMs);
void update(bool downtime, uint32_t totalParts, uint32_t rejectParts);
void setIdealCycleTimeMs(uint32_t idealCycleTimeMs);
void setTargetQuantity(uint32_t targetQuantity);
void recordLoss(uint16_t lossCode, uint32_t durationSeconds);
OEESnapshot snapshot();
uint32_t lossSeconds(uint16_t lossCode);
void resetShift();
}
