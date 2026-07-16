#include "RejectManager.h"

namespace {
volatile uint32_t gTotal = 0;
volatile uint32_t gPending = 0;
volatile uint32_t gLastPulseUs = 0;
volatile bool gEnabled = true;
uint32_t gDebounceUs = 50000UL;

void IRAM_ATTR onPulse() {
  if (!gEnabled) return;
  const uint32_t now = micros();
  if (now - gLastPulseUs < gDebounceUs) return;
  gLastPulseUs = now;
  ++gTotal;
  ++gPending;
}
}

void RejectManager::begin(uint8_t pin, uint32_t debounceUs) {
  gDebounceUs = debounceUs;
  gEnabled = true;
  pinMode(pin, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(pin), onPulse, FALLING);
}

void RejectManager::setEnabled(bool enabled) {
  noInterrupts();
  gEnabled = enabled;
  if (!enabled) gPending = 0;
  interrupts();
}

bool RejectManager::enabled() {
  noInterrupts();
  const bool value = gEnabled;
  interrupts();
  return value;
}

bool RejectManager::consumePulse() {
  noInterrupts();
  const bool available = gPending > 0;
  if (available) --gPending;
  interrupts();
  return available;
}

uint32_t RejectManager::total() {
  noInterrupts();
  const uint32_t value = gTotal;
  interrupts();
  return value;
}

void RejectManager::restore(uint32_t total) {
  noInterrupts();
  gTotal = total;
  gPending = 0;
  interrupts();
}

void RejectManager::reset() {
  restore(0);
}
