#include "ProductionManager.h"

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

void ProductionManager::begin(uint8_t pin, uint32_t debounceUs) {
  gDebounceUs = debounceUs;
  gEnabled = true;
  pinMode(pin, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(pin), onPulse, FALLING);
}

bool ProductionManager::consumePulse() {
  noInterrupts();
  const bool available = gPending > 0;
  if (available) --gPending;
  interrupts();
  return available;
}

uint32_t ProductionManager::total() {
  noInterrupts();
  const uint32_t value = gTotal;
  interrupts();
  return value;
}

void ProductionManager::restore(uint32_t total) {
  noInterrupts();
  gTotal = total;
  gPending = 0;
  interrupts();
}

void ProductionManager::reset() {
  restore(0);
}

void ProductionManager::setEnabled(bool enabled) {
  noInterrupts();
  gEnabled = enabled;
  if (!enabled) gPending = 0;
  interrupts();
}

bool ProductionManager::enabled() {
  noInterrupts();
  const bool value = gEnabled;
  interrupts();
  return value;
}
