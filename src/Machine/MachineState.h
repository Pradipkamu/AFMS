#pragma once
#include <Arduino.h>

enum class MachineState : uint8_t {
  Ready,
  Running,
  Idle,
  LossRequired
};

struct MachineSnapshot {
  MachineState state;
  uint32_t totalParts;
  uint32_t rejectParts;
  uint32_t goodParts;
  uint32_t lastProductionMs;
  uint32_t idleSeconds;
  bool alarmActive;
};
