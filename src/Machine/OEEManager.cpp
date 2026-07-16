#include "OEEManager.h"

namespace {
constexpr uint16_t kPlannedShutdownLossCode = 1;
uint32_t gLastUpdateMs = 0;
uint32_t gMeasuredElapsedMs = 0;
uint32_t gScheduledElapsedMs = 0;
uint32_t gPlannedShutdownMs = 0;
uint32_t gDowntimeMs = 0;
uint32_t gIdealCycleTimeMs = 1000;
uint32_t gTargetQuantity = 0;
uint32_t gLifetimeProduction = 0;
uint32_t gLifetimeReject = 0;
uint32_t gProductionBaseline = 0;
uint32_t gRejectBaseline = 0;
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
  gMeasuredElapsedMs = gScheduledElapsedMs = 0;
  gPlannedShutdownMs = gDowntimeMs = 0;
  gIdealCycleTimeMs = idealCycleTimeMs > 0 ? idealCycleTimeMs : 1000;
  gTargetQuantity = 0;
  gLifetimeProduction = gLifetimeReject = 0;
  gProductionBaseline = gRejectBaseline = 0;
  for (uint8_t i = 0; i < 17; ++i) gLossSeconds[i] = 0;
}

void OEEManager::update(bool downtime, uint32_t totalParts, uint32_t rejectParts) {
  const uint32_t now = millis();
  const uint32_t elapsed = now - gLastUpdateMs;
  gLastUpdateMs = now;
  gMeasuredElapsedMs += elapsed;
  if (downtime) gDowntimeMs += elapsed;
  gLifetimeProduction = totalParts;
  gLifetimeReject = rejectParts;
}

void OEEManager::setIdealCycleTimeMs(uint32_t idealCycleTimeMs) {
  if (idealCycleTimeMs > 0) gIdealCycleTimeMs = idealCycleTimeMs;
}

void OEEManager::setTargetQuantity(uint32_t targetQuantity) { gTargetQuantity = targetQuantity; }

void OEEManager::setScheduledShiftElapsedSeconds(uint32_t elapsedSeconds) {
  gScheduledElapsedMs = elapsedSeconds * 1000UL;
}

void OEEManager::setCounterBaselines(uint32_t totalParts, uint32_t rejectParts) {
  gProductionBaseline = totalParts;
  gRejectBaseline = rejectParts;
}

void OEEManager::recordLoss(uint16_t lossCode, uint32_t durationSeconds) {
  if (lossCode < 1 || lossCode > 16) return;
  gLossSeconds[lossCode] += durationSeconds;
  if (lossCode == kPlannedShutdownLossCode && durationSeconds > 0) {
    const uint32_t durationMs = durationSeconds * 1000UL;
    const uint32_t removableMs = durationMs < gDowntimeMs ? durationMs : gDowntimeMs;
    gDowntimeMs = subtractFloor(gDowntimeMs, removableMs);
    gPlannedShutdownMs += removableMs;
  }
}

OEESnapshot OEEManager::snapshot() {
  const uint32_t shiftProduction = subtractFloor(gLifetimeProduction, gProductionBaseline);
  const uint32_t shiftReject = subtractFloor(gLifetimeReject, gRejectBaseline);
  const uint32_t goodParts = shiftProduction > shiftReject ? shiftProduction - shiftReject : 0;
  const uint32_t scheduledBaseMs = gScheduledElapsedMs > 0 ? gScheduledElapsedMs : gMeasuredElapsedMs;
  const uint32_t plannedProductionMs = subtractFloor(scheduledBaseMs, gPlannedShutdownMs);
  const uint32_t boundedDowntimeMs = gDowntimeMs < plannedProductionMs ? gDowntimeMs : plannedProductionMs;
  const uint32_t calculatedRunMs = plannedProductionMs - boundedDowntimeMs;

  OEESnapshot value;
  value.scheduledShiftElapsedSeconds = scheduledBaseMs / 1000UL;
  value.plannedSeconds = plannedProductionMs / 1000UL;
  value.plannedShutdownSeconds = gPlannedShutdownMs / 1000UL;
  value.runSeconds = calculatedRunMs / 1000UL;
  value.downtimeSeconds = boundedDowntimeMs / 1000UL;
  value.targetQuantity = gTargetQuantity;
  value.availabilityPermille = toPermille(calculatedRunMs, plannedProductionMs);
  value.performancePermille = toPermille(static_cast<uint64_t>(gIdealCycleTimeMs) * shiftProduction, calculatedRunMs);
  value.qualityPermille = shiftProduction == 0 ? 1000 : toPermille(goodParts, shiftProduction);
  value.oeePermille = static_cast<uint16_t>((static_cast<uint32_t>(value.availabilityPermille) * value.performancePermille * value.qualityPermille) / 1000000UL);
  return value;
}

uint32_t OEEManager::lossSeconds(uint16_t lossCode) {
  return (lossCode >= 1 && lossCode <= 16) ? gLossSeconds[lossCode] : 0;
}

OEEPersistentState OEEManager::persistentState() {
  OEEPersistentState state = {};
  state.measuredElapsedMs = gMeasuredElapsedMs;
  state.plannedShutdownMs = gPlannedShutdownMs;
  state.downtimeMs = gDowntimeMs;
  state.productionBaseline = gProductionBaseline;
  state.rejectBaseline = gRejectBaseline;
  for (uint8_t i = 0; i < 17; ++i) state.lossSeconds[i] = gLossSeconds[i];
  return state;
}

void OEEManager::restorePersistentState(const OEEPersistentState &state) {
  gMeasuredElapsedMs = state.measuredElapsedMs;
  gPlannedShutdownMs = state.plannedShutdownMs;
  gDowntimeMs = state.downtimeMs;
  gProductionBaseline = state.productionBaseline;
  gRejectBaseline = state.rejectBaseline;
  for (uint8_t i = 0; i < 17; ++i) gLossSeconds[i] = state.lossSeconds[i];
  gLastUpdateMs = millis();
}

void OEEManager::resetShift() {
  gLastUpdateMs = millis();
  gMeasuredElapsedMs = gScheduledElapsedMs = 0;
  gPlannedShutdownMs = gDowntimeMs = 0;
  gProductionBaseline = gLifetimeProduction;
  gRejectBaseline = gLifetimeReject;
  for (uint8_t i = 0; i < 17; ++i) gLossSeconds[i] = 0;
}