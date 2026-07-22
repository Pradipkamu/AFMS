#include "CycleManager.h"

namespace {
uint32_t gCycleTimeMs = 20000UL;
uint32_t gLastProductionMs = 0;
uint32_t gCompletionTimeMs = 0;
uint32_t gCycleEndTimeoutMs = 180000UL;
uint32_t gTimeoutCount = 0;
uint32_t gDuplicateStartCount = 0;
bool gCycleEndEnabled = false;
bool gCycleInProgress = false;
CycleManager::CompletionReason gCompletionReason = CycleManager::CompletionReason::None;
}

void CycleManager::begin(uint32_t cycleTimeMs, bool cycleEndEnabled, uint32_t cycleEndTimeoutMs) {
  gCycleTimeMs = cycleTimeMs;
  gCycleEndEnabled = cycleEndEnabled;
  gCycleEndTimeoutMs = cycleEndTimeoutMs >= 1000UL ? cycleEndTimeoutMs : 180000UL;
  gLastProductionMs = millis();
  gCompletionTimeMs = 0;
  gTimeoutCount = 0;
  gDuplicateStartCount = 0;
  gCycleInProgress = false;
  gCompletionReason = CompletionReason::None;
}

void CycleManager::setCycleTimeMs(uint32_t cycleTimeMs) {
  if (cycleTimeMs >= 100UL) gCycleTimeMs = cycleTimeMs;
}

bool CycleManager::onProduction(uint32_t nowMs) {
  if (gCycleEndEnabled && gCycleInProgress) {
    ++gDuplicateStartCount;
    return false;
  }
  gLastProductionMs = nowMs;
  gCompletionTimeMs = 0;
  gCycleInProgress = true;
  gCompletionReason = CompletionReason::None;
  return true;
}

bool CycleManager::onCycleEnd(uint32_t nowMs) {
  if (!gCycleEndEnabled || !gCycleInProgress) return false;
  if (static_cast<int32_t>(nowMs - gLastProductionMs) < 0) return false;
  gCompletionTimeMs = nowMs;
  gCycleInProgress = false;
  gCompletionReason = CompletionReason::CycleEndInput;
  return true;
}

void CycleManager::armWaitingForStart(uint32_t nowMs) {
  gCycleInProgress = false;
  gCompletionTimeMs = nowMs;
  gCompletionReason = CompletionReason::WaitingForStart;
}

void CycleManager::update(uint32_t nowMs) {
  if (!gCycleInProgress) return;

  const uint32_t elapsed = nowMs - gLastProductionMs;
  if (!gCycleEndEnabled && elapsed >= gCycleTimeMs) {
    gCompletionTimeMs = gLastProductionMs + gCycleTimeMs;
    gCycleInProgress = false;
    gCompletionReason = CompletionReason::FixedTime;
    return;
  }

  if (gCycleEndEnabled) {
    // In Cycle End mode, the configured cycle time remains the expected
    // machine-processing period. The Cycle End timeout is an additional
    // grace period after that expected cycle time, not a replacement for it.
    const uint32_t cycleEndDeadlineMs = gCycleTimeMs + gCycleEndTimeoutMs;
    if (elapsed >= cycleEndDeadlineMs) {
      gCompletionTimeMs = gLastProductionMs + cycleEndDeadlineMs;
      gCycleInProgress = false;
      gCompletionReason = CompletionReason::CycleEndTimeout;
      ++gTimeoutCount;
    }
  }
}

uint32_t CycleManager::cycleTimeMs() { return gCycleTimeMs; }
uint32_t CycleManager::lastProductionMs() { return gLastProductionMs; }
uint32_t CycleManager::completionTimeMs() { return gCompletionTimeMs; }
bool CycleManager::cycleCompleted() { return gCompletionTimeMs != 0 && !gCycleInProgress; }
bool CycleManager::cycleInProgress() { return gCycleInProgress; }
bool CycleManager::cycleEndEnabled() { return gCycleEndEnabled; }
CycleManager::CompletionReason CycleManager::completionReason() { return gCompletionReason; }
uint32_t CycleManager::timeoutCount() { return gTimeoutCount; }
uint32_t CycleManager::duplicateStartCount() { return gDuplicateStartCount; }
