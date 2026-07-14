#include "EventBus.h"

namespace {
constexpr uint8_t kCapacity = 32;
Event gQueue[kCapacity];
volatile uint8_t gHead = 0;
volatile uint8_t gTail = 0;
uint16_t gDropped = 0;
}

void EventBus::begin() {
  noInterrupts();
  gHead = gTail = 0;
  gDropped = 0;
  interrupts();
  publish(EventType::SystemBoot);
}

bool EventBus::publish(EventType type, int32_t value) {
  noInterrupts();
  const uint8_t nextHead = static_cast<uint8_t>((gHead + 1U) % kCapacity);
  if (nextHead == gTail) {
    ++gDropped;
    interrupts();
    return false;
  }
  gQueue[gHead] = {type, millis(), value};
  gHead = nextHead;
  interrupts();
  return true;
}

bool EventBus::next(Event &event) {
  noInterrupts();
  if (gHead == gTail) {
    interrupts();
    return false;
  }
  event = gQueue[gTail];
  gTail = static_cast<uint8_t>((gTail + 1U) % kCapacity);
  interrupts();
  return true;
}

void EventBus::update() {}
uint16_t EventBus::droppedCount() { return gDropped; }
