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

void IdleManager::update(bool cycleCompleted, uint32_t nowMs, uint32_t completionTimeMs) {
  if (!cycleCompleted || completionTimeMs == 0) {
    gIdle = false;
    gIdleSeconds = 0;
    gAlarmDue = false;
    return;
  }

  gIdle = true;
  const uint32_t elapsedIdleMs = nowMs - completionTimeMs;
  gIdleSeconds = elapsedIdleMs / 1000UL;
  gAlarmDue = elapsedIdleMs >= gIdleDelayMs;
}

bool IdleManager::idle() { return gIdle; }
bool IdleManager::alarmDue() { return gAlarmDue; }
uint32_t IdleManager::idleSeconds() { return gIdleSeconds; }
