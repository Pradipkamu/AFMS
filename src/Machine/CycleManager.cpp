#include "CycleManager.h"

namespace {
uint32_t gCycleTimeMs = 20000UL;
uint32_t gLastProductionMs = 0;
}

void CycleManager::begin(uint32_t cycleTimeMs) {
  gCycleTimeMs = cycleTimeMs;
  gLastProductionMs = millis();
}

void CycleManager::setCycleTimeMs(uint32_t cycleTimeMs) {
  if (cycleTimeMs >= 100UL) gCycleTimeMs = cycleTimeMs;
}

void CycleManager::onProduction(uint32_t nowMs) { gLastProductionMs = nowMs; }
uint32_t CycleManager::cycleTimeMs() { return gCycleTimeMs; }
uint32_t CycleManager::lastProductionMs() { return gLastProductionMs; }
bool CycleManager::cycleExpired(uint32_t nowMs) { return nowMs - gLastProductionMs >= gCycleTimeMs; }
