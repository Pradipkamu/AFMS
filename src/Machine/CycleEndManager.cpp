#include "CycleEndManager.h"

namespace {
uint8_t gPin = 0;
bool gActiveHigh = true;
uint32_t gDebounceMs = 50;
bool gRawActive = false;
bool gStableActive = false;
bool gPulsePending = false;
uint32_t gRawChangedMs = 0;

bool readActive() {
  const bool levelHigh = digitalRead(gPin) == HIGH;
  return gActiveHigh ? levelHigh : !levelHigh;
}
}

void CycleEndManager::begin(uint8_t pin, bool activeHigh, uint16_t debounceMs) {
  gPin = pin;
  gActiveHigh = activeHigh;
  gDebounceMs = debounceMs;
  pinMode(gPin, gActiveHigh ? INPUT : INPUT_PULLUP);
  gRawActive = readActive();
  gStableActive = gRawActive;
  gPulsePending = false;
  gRawChangedMs = millis();
}

void CycleEndManager::update() {
  const uint32_t nowMs = millis();
  const bool current = readActive();
  if (current != gRawActive) {
    gRawActive = current;
    gRawChangedMs = nowMs;
  }
  if (gStableActive != gRawActive && nowMs - gRawChangedMs >= gDebounceMs) {
    gStableActive = gRawActive;
    if (gStableActive) gPulsePending = true;
  }
}

bool CycleEndManager::consumePulse() {
  if (!gPulsePending) return false;
  gPulsePending = false;
  return true;
}

bool CycleEndManager::active() { return gStableActive; }
