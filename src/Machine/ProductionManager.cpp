#include "ProductionManager.h"

namespace {
// 63 usable entries. At a one-second cycle this tolerates more than a minute
// of foreground blocking without losing an accepted production pulse.
constexpr uint8_t kQueueSize = 64;
volatile uint32_t gTotal = 0;
volatile uint32_t gPulseTimesUs[kQueueSize] = {0};
volatile uint8_t gHead = 0;
volatile uint8_t gTail = 0;
volatile uint32_t gLastPulseUs = 0;
volatile uint32_t gDropped = 0;
volatile bool gEnabled = true;
uint32_t gDebounceUs = 50000UL;

void IRAM_ATTR onPulse() {
  if (!gEnabled) return;
  const uint32_t nowUs = micros();
  if (gLastPulseUs != 0 && nowUs - gLastPulseUs < gDebounceUs) return;
  gLastPulseUs = nowUs;

  const uint8_t next = static_cast<uint8_t>((gHead + 1U) % kQueueSize);
  if (next == gTail) {
    ++gDropped;
    return;
  }
  gPulseTimesUs[gHead] = nowUs;
  gHead = next;
}
}

void ProductionManager::begin(uint8_t pin, uint32_t debounceUs) {
  noInterrupts();
  gDebounceUs = debounceUs;
  gEnabled = true;
  gHead = gTail = 0;
  gLastPulseUs = 0;
  gDropped = 0;
  interrupts();
  pinMode(pin, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(pin), onPulse, FALLING);
}

bool ProductionManager::consumePulse(uint32_t &timestampUs) {
  noInterrupts();
  if (gTail == gHead) {
    interrupts();
    return false;
  }
  timestampUs = gPulseTimesUs[gTail];
  gTail = static_cast<uint8_t>((gTail + 1U) % kQueueSize);
  interrupts();
  return true;
}

void ProductionManager::acceptPulse() {
  noInterrupts();
  ++gTotal;
  interrupts();
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
  gHead = gTail = 0;
  gLastPulseUs = 0;
  interrupts();
}

void ProductionManager::reset() { restore(0); }

void ProductionManager::setEnabled(bool enabled) {
  noInterrupts();
  gEnabled = enabled;
  if (!enabled) gHead = gTail = 0;
  gLastPulseUs = 0;
  interrupts();
}

bool ProductionManager::enabled() {
  noInterrupts();
  const bool value = gEnabled;
  interrupts();
  return value;
}

uint32_t ProductionManager::droppedPulseCount() {
  noInterrupts();
  const uint32_t value = gDropped;
  interrupts();
  return value;
}
