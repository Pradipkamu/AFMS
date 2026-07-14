#pragma once
#include <Arduino.h>

enum class EventType : uint8_t {
  SystemBoot,
  WifiConnected,
  WifiDisconnected,
  MachineReady,
  ProductionPulse,
  RejectPulse,
  IdleStarted,
  IdleEnded,
  AlarmActivated,
  AlarmCleared,
  LossSelected
};

struct Event {
  EventType type;
  uint32_t timestampMs;
  int32_t value;
};

namespace EventBus {
void begin();
bool publish(EventType type, int32_t value = 0);
bool next(Event &event);
void update();
uint16_t droppedCount();
}
