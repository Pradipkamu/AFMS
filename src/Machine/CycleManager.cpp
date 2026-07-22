#include "CycleManager.h"

namespace {
uint32_t gCycleTimeMs = 20000UL;
uint32_t gLastProductionMs = 0;
uint32_t gCompletionTimeMs = 0;
uint32_t gCycleEndTimeoutMs = 180000UL;
uint32_t gCycleEndMinimumMs = 50UL;
uint32_t gTimeoutCount = 0;
uint32_t gDuplicateStartCount = 0;
uint32_t gEarlyCycleEndCount = 0;
bool gCycleEndEnabled = false;
bool gCycleInProgress = false;
CycleManager::CompletionReason gCompletionReason = CycleManager::CompletionReason::None;
}

void CycleManager::begin(uint32_t cycleTimeMs,
                         bool cycleEndEnabled,
                         uint32_t cycleEndTimeoutMs,
                         uint32_t cycleEndMinimumMs) {
  gCycleTimeMs = cycleTimeMs;
  gCycleEndEnabled = cycleEndEnabled;
  gCycleEndTimeoutMs = cycleEndTimeoutMs >= 1000UL ? cycleEndTimeoutMs : 180000UL;
  gCycleEndMinimumMs = cycleEndMinimumMs >= 1UL ? cycleEndMinimumMs : 50UL;
  gLastProductionMs = millis();
  gCompletionTimeMs = 0;
  gTimeoutCount = 0;
  gDuplicateStartCount = 0;
  gEarlyCycleEndCount = 0;
  gCycleInProgress = false;
  gCompletionReason = CompletionReason::None;
}

void CycleManager::setCycleTimeMs(uint32_t cycleTimeMs) {
  if (cycleTimeMs >= 100UL) gCycleTimeMs = cycleTimeMs;
}

bool CycleManager::onProduction(uint32_t nowMs) {
  if (gCycleInProgress) {
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
  const uint32_t elapsed = nowMs - gLastProductionMs;
  if (elapsed < gCycleEndMinimumMs) {
    ++gEarlyCycleEndCount;
    return false;
  }
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
uint32_t CycleManager::earlyCycleEndCount() { return gEarlyCycleEndCount; }
