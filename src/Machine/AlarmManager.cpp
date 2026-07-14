#include "AlarmManager.h"

namespace {
uint8_t gPin = 255;
bool gActiveHigh = true;
bool gActive = false;

void writeOutput() {
  if (gPin == 255) return;
  digitalWrite(gPin, gActive == gActiveHigh ? HIGH : LOW);
}
}

void AlarmManager::begin(uint8_t pin, bool activeHigh) {
  gPin = pin;
  gActiveHigh = activeHigh;
  pinMode(gPin, OUTPUT);
  gActive = false;
  writeOutput();
}

void AlarmManager::set(bool active) {
  if (gActive == active) return;
  gActive = active;
  writeOutput();
}

void AlarmManager::clear() { set(false); }
bool AlarmManager::active() { return gActive; }
