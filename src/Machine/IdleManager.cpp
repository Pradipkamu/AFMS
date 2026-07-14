#include "IdleManager.h"

namespace {
uint32_t gIdleDelayMs = 600000UL;
bool gIdle = false;
bool gAlarmDue = false;
uint32_t gIdleSeconds = 0;
}

void IdleManager::begin(uint32_t idleDelayMs) {
  gIdleDelayMs = idleDelayMs;
  gIdle = false;
  gAlarmDue = false;
  gIdleSeconds = 0;
}

void IdleManager::setIdleDelayMs(uint32_t idleDelayMs) {
  if (idleDelayMs >= 1000UL) gIdleDelayMs = idleDelayMs;
}

void IdleManager::onProduction() {
  gIdle = false;
  gAlarmDue = false;
  gIdleSeconds = 0;
}

void IdleManager::update(bool cycleExpired, uint32_t nowMs, uint32_t lastProductionMs, uint32_t cycleTimeMs) {
  if (!cycleExpired) {
    gIdle = false;
    gIdleSeconds = 0;
    return;
  }

  gIdle = true;
  const uint32_t idleStartMs = lastProductionMs + cycleTimeMs;
  const uint32_t elapsedIdleMs = nowMs - idleStartMs;
  gIdleSeconds = elapsedIdleMs / 1000UL;
  gAlarmDue = elapsedIdleMs >= gIdleDelayMs;
}

bool IdleManager::idle() { return gIdle; }
bool IdleManager::alarmDue() { return gAlarmDue; }
uint32_t IdleManager::idleSeconds() { return gIdleSeconds; }
