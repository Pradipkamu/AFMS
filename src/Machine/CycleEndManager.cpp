#include "CycleEndManager.h"

namespace {
uint8_t gPin = 0;
bool gActiveHigh = true;
volatile uint32_t gLastPulseUs = 0;
volatile bool gPulsePending = false;
uint32_t gDebounceUs = 50000UL;

void IRAM_ATTR onCycleEndPulse() {
  const uint32_t nowUs = micros();
  if (nowUs - gLastPulseUs < gDebounceUs) return;
  gLastPulseUs = nowUs;
  gPulsePending = true;
}
}

void CycleEndManager::begin(uint8_t pin, bool activeHigh, uint16_t debounceMs) {
  gPin = pin;
  gActiveHigh = activeHigh;
  gDebounceUs = static_cast<uint32_t>(debounceMs) * 1000UL;
  gPulsePending = false;
  gLastPulseUs = 0;

  // Active-low inputs use the ESP8266 internal pull-up. Active-high inputs
  // require an external pull-down in the isolated machine input circuit.
  pinMode(gPin, gActiveHigh ? INPUT : INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(gPin),
                  onCycleEndPulse,
                  gActiveHigh ? RISING : FALLING);
}

void CycleEndManager::update() {
  // Capture is interrupt-driven so short Cycle End pulses are retained even
  // while network or filesystem work temporarily delays the main loop.
}

bool CycleEndManager::consumePulse() {
  noInterrupts();
  const bool available = gPulsePending;
  gPulsePending = false;
  interrupts();
  return available;
}

bool CycleEndManager::active() {
  const bool levelHigh = digitalRead(gPin) == HIGH;
  return gActiveHigh ? levelHigh : !levelHigh;
}
