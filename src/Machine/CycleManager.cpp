#include "CycleManager.h"

namespace {
uint32_t gCycleTimeMs = 20000UL;
uint32_t gLastProductionMs = 0;
uint32_t gCompletionTimeMs = 0;
uint32_t gCycleEndTimeoutMs = 180000UL;
uint32_t gTimeoutCount = 0;
bool gCycleEndEnabled = false;
bool gCycleInProgress = false;
}

void CycleManager::begin(uint32_t cycleTimeMs, bool cycleEndEnabled, uint32_t cycleEndTimeoutMs) {
  gCycleTimeMs = cycleTimeMs;
  gCycleEndEnabled = cycleEndEnabled;
  gCycleEndTimeoutMs = cycleEndTimeoutMs >= 1000UL ? cycleEndTimeoutMs : 180000UL;
  gLastProductionMs = millis();
  gCompletionTimeMs = 0;
  gTimeoutCount = 0;
  gCycleInProgress = false;
}

void CycleManager::setCycleTimeMs(uint32_t cycleTimeMs) {
  if (cycleTimeMs >= 100UL) gCycleTimeMs = cycleTimeMs;
}

void CycleManager::onProduction(uint32_t nowMs) {
  gLastProductionMs = nowMs;
  gCompletionTimeMs = 0;
  gCycleInProgress = true;
}

bool CycleManager::onCycleEnd(uint32_t nowMs) {
  if (!gCycleEndEnabled || !gCycleInProgress) return false;
  gCompletionTimeMs = nowMs;
  gCycleInProgress = false;
  return true;
}

void CycleManager::update(uint32_t nowMs) {
  if (!gCycleInProgress) return;
  const uint32_t elapsed = nowMs - gLastProductionMs;
  if (!gCycleEndEnabled && elapsed >= gCycleTimeMs) {
    gCompletionTimeMs = gLastProductionMs + gCycleTimeMs;
    gCycleInProgress = false;
  } else if (gCycleEndEnabled && elapsed >= gCycleEndTimeoutMs) {
    gCompletionTimeMs = gLastProductionMs + gCycleEndTimeoutMs;
    gCycleInProgress = false;
    ++gTimeoutCount;
  }
}

uint32_t CycleManager::cycleTimeMs() { return gCycleTimeMs; }
uint32_t CycleManager::lastProductionMs() { return gLastProductionMs; }
uint32_t CycleManager::completionTimeMs() { return gCompletionTimeMs; }
bool CycleManager::cycleCompleted() { return gCompletionTimeMs != 0 && !gCycleInProgress; }
bool CycleManager::cycleInProgress() { return gCycleInProgress; }
bool CycleManager::cycleEndEnabled() { return gCycleEndEnabled; }
uint32_t CycleManager::timeoutCount() { return gTimeoutCount; }
