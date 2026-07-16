#include "OEEManager.h"

namespace {
constexpr uint16_t kPlannedShutdownLossCode = 1;

uint32_t gLastUpdateMs = 0;
uint32_t gPlannedMs = 0;
uint32_t gPlannedShutdownMs = 0;
uint32_t gRunMs = 0;
uint32_t gDowntimeMs = 0;
uint32_t gIdealCycleTimeMs = 1000;
uint32_t gTargetQuantity = 0;
uint32_t gTotalParts = 0;
uint32_t gRejectParts = 0;
uint32_t gLossSeconds[17] = {0};

uint16_t toPermille(uint64_t numerator, uint64_t denominator) {
  if (denominator == 0) return 0;
  uint64_t value = (numerator * 1000ULL) / denominator;
  if (value > 1000ULL) value = 1000ULL;
  return static_cast<uint16_t>(value);
}

uint32_t subtractFloor(uint32_t value, uint32_t amount) {
  return value > amount ? value - amount : 0;
}
}

void OEEManager::begin(uint32_t idealCycleTimeMs) {
  gLastUpdateMs = millis();
  gPlannedMs = gPlannedShutdownMs = gRunMs = gDowntimeMs = 0;
  gIdealCycleTimeMs = idealCycleTimeMs > 0 ? idealCycleTimeMs : 1000;
  gTargetQuantity = 0;
  gTotalParts = gRejectParts = 0;
  for (uint8_t i = 0; i < 17; ++i) gLossSeconds[i] = 0;
}

void OEEManager::update(bool downtime, uint32_t totalParts, uint32_t rejectParts) {
  const uint32_t now = millis();
  const uint32_t elapsed = now - gLastUpdateMs;
  gLastUpdateMs = now;
  gPlannedMs += elapsed;
  if (downtime) gDowntimeMs += elapsed;
  else gRunMs += elapsed;
  gTotalParts = totalParts;
  gRejectParts = rejectParts;
}

void OEEManager::setIdealCycleTimeMs(uint32_t idealCycleTimeMs) {
  if (idealCycleTimeMs > 0) gIdealCycleTimeMs = idealCycleTimeMs;
}

void OEEManager::setTargetQuantity(uint32_t targetQuantity) { gTargetQuantity = targetQuantity; }

void OEEManager::recordLoss(uint16_t lossCode, uint32_t durationSeconds) {
  if (lossCode < 1 || lossCode > 16) return;

  gLossSeconds[lossCode] += durationSeconds;

  if (lossCode == kPlannedShutdownLossCode && durationSeconds > 0) {
    const uint32_t durationMs = durationSeconds * 1000UL;
    const uint32_t removableMs = durationMs < gDowntimeMs ? durationMs : gDowntimeMs;

    gDowntimeMs = subtractFloor(gDowntimeMs, removableMs);
    gPlannedMs = subtractFloor(gPlannedMs, removableMs);
    gPlannedShutdownMs += removableMs;
  }
}

OEESnapshot OEEManager::snapshot() {
  const uint32_t goodParts = gTotalParts > gRejectParts ? gTotalParts - gRejectParts : 0;
  OEESnapshot value;
  value.plannedSeconds = gPlannedMs / 1000UL;
  value.plannedShutdownSeconds = gPlannedShutdownMs / 1000UL;
  value.runSeconds = gRunMs / 1000UL;
  value.downtimeSeconds = gDowntimeMs / 1000UL;
  value.targetQuantity = gTargetQuantity;
  value.availabilityPermille = toPermille(gRunMs, gPlannedMs);
  value.performancePermille = toPermille(static_cast<uint64_t>(gIdealCycleTimeMs) * gTotalParts, gRunMs);
  value.qualityPermille = gTotalParts == 0 ? 1000 : toPermille(goodParts, gTotalParts);
  value.oeePermille = static_cast<uint16_t>((static_cast<uint32_t>(value.availabilityPermille) * value.performancePermille * value.qualityPermille) / 1000000UL);
  return value;
}

uint32_t OEEManager::lossSeconds(uint16_t lossCode) {
  return (lossCode >= 1 && lossCode <= 16) ? gLossSeconds[lossCode] : 0;
}

void OEEManager::resetShift() {
  gLastUpdateMs = millis();
  gPlannedMs = gPlannedShutdownMs = gRunMs = gDowntimeMs = 0;
  gTotalParts = gRejectParts = 0;
  for (uint8_t i = 0; i < 17; ++i) gLossSeconds[i] = 0;
}
