#include "ProductionManager.h"

namespace {
volatile uint32_t gTotal = 0;
volatile uint32_t gPending = 0;
volatile uint32_t gLastPulseUs = 0;
uint32_t gDebounceUs = 50000UL;

void IRAM_ATTR onPulse() {
  const uint32_t now = micros();
  if (now - gLastPulseUs < gDebounceUs) return;
  gLastPulseUs = now;
  ++gTotal;
  ++gPending;
}
}

void ProductionManager::begin(uint8_t pin, uint32_t debounceUs) {
  gDebounceUs = debounceUs;
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

void ProductionManager::reset() {
  noInterrupts();
  gTotal = 0;
  gPending = 0;
  interrupts();
}
