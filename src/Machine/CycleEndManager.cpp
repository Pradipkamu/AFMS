#include "CycleEndManager.h"

namespace {
constexpr uint8_t kQueueSize = 8;
uint8_t gPin = 0;
bool gActiveHigh = true;
volatile uint32_t gPulseTimesMs[kQueueSize] = {0};
volatile uint8_t gHead = 0;
volatile uint8_t gTail = 0;
volatile uint32_t gLastPulseUs = 0;
volatile uint32_t gDropped = 0;
uint32_t gDebounceUs = 50000UL;

void IRAM_ATTR onCycleEndPulse() {
  const uint32_t nowUs = micros();
  if (nowUs - gLastPulseUs < gDebounceUs) return;
  gLastPulseUs = nowUs;

  const uint8_t next = static_cast<uint8_t>((gHead + 1U) % kQueueSize);
  if (next == gTail) {
    ++gDropped;
    return;
  }
  gPulseTimesMs[gHead] = nowUs / 1000UL;
  gHead = next;
}
}

void CycleEndManager::begin(uint8_t pin, bool activeHigh, uint16_t debounceMs) {
  gPin = pin;
  gActiveHigh = activeHigh;
  gDebounceUs = static_cast<uint32_t>(debounceMs) * 1000UL;
  gHead = gTail = 0;
  gLastPulseUs = 0;
  gDropped = 0;

  pinMode(gPin, gActiveHigh ? INPUT : INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(gPin),
                  onCycleEndPulse,
                  gActiveHigh ? RISING : FALLING);
}

void CycleEndManager::update() {}

bool CycleEndManager::consumePulse(uint32_t &timestampMs) {
  noInterrupts();
  if (gTail == gHead) {
    interrupts();
    return false;
  }
  timestampMs = gPulseTimesMs[gTail];
  gTail = static_cast<uint8_t>((gTail + 1U) % kQueueSize);
  interrupts();
  return true;
}

bool CycleEndManager::active() {
  const bool levelHigh = digitalRead(gPin) == HIGH;
  return gActiveHigh ? levelHigh : !levelHigh;
}

uint32_t CycleEndManager::droppedPulseCount() {
  noInterrupts();
  const uint32_t value = gDropped;
  interrupts();
  return value;
}
