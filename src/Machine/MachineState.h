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
  uint32_t runSeconds;
  uint32_t downtimeSeconds;
  uint32_t targetQuantity;
  uint16_t availabilityPermille;
  uint16_t performancePermille;
  uint16_t qualityPermille;
  uint16_t oeePermille;
  bool alarmActive;
};
