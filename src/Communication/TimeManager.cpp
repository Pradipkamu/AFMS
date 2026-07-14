#include "TimeManager.h"
#include <time.h>

namespace {
bool gStarted = false;
bool gSynced = false;
uint32_t gLastCheckMs = 0;
}

void TimeManager::begin(long utcOffsetSeconds, int daylightOffsetSeconds) {
  configTime(utcOffsetSeconds, daylightOffsetSeconds, "pool.ntp.org", "time.nist.gov");
  gStarted = true;
  gSynced = false;
  gLastCheckMs = 0;
}

void TimeManager::update() {
  if (!gStarted || millis() - gLastCheckMs < 1000UL) return;
  gLastCheckMs = millis();
  const time_t current = time(nullptr);
  gSynced = current > 1700000000;
}

bool TimeManager::synchronized() { return gSynced; }
time_t TimeManager::now() { return time(nullptr); }

String TimeManager::iso8601() {
  const time_t current = time(nullptr);
  struct tm localTime;
  if (!localtime_r(&current, &localTime)) return String();
  char buffer[25];
  strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%S%z", &localTime);
  return String(buffer);
}
