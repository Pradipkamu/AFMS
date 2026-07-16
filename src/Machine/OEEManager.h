#pragma once
#include <Arduino.h>

struct OEESnapshot {
  uint32_t scheduledShiftElapsedSeconds;
  uint32_t plannedSeconds;
  uint32_t plannedShutdownSeconds;
  uint32_t runSeconds;
  uint32_t downtimeSeconds;
  uint32_t targetQuantity;
  uint16_t availabilityPermille;
  uint16_t performancePermille;
  uint16_t qualityPermille;
  uint16_t oeePermille;
};

struct OEEPersistentState {
  uint32_t measuredElapsedMs;
  uint32_t plannedShutdownMs;
  uint32_t downtimeMs;
  uint32_t productionBaseline;
  uint32_t rejectBaseline;
  uint32_t lossSeconds[17];
};

namespace OEEManager {
void begin(uint32_t idealCycleTimeMs);
void update(bool downtime, uint32_t totalParts, uint32_t rejectParts);
void setIdealCycleTimeMs(uint32_t idealCycleTimeMs);
void setTargetQuantity(uint32_t targetQuantity);
void setScheduledShiftElapsedSeconds(uint32_t elapsedSeconds);
void setCounterBaselines(uint32_t totalParts, uint32_t rejectParts);
void recordLoss(uint16_t lossCode, uint32_t durationSeconds);
OEESnapshot snapshot();
uint32_t lossSeconds(uint16_t lossCode);
OEEPersistentState persistentState();
void restorePersistentState(const OEEPersistentState &state);
void resetShift();
}