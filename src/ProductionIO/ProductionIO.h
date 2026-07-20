#pragma once

#include <Arduino.h>

namespace ProductionIO {

enum class InputId : uint8_t {
  Production = 0,
  Reject = 1,
  Count = 2
};

enum class OutputId : uint8_t {
  Alarm = 0,
  Count = 1
};

struct InputSnapshot {
  bool raw = false;
  bool stable = false;
  uint32_t changeCount = 0;
  uint32_t lastChangeMs = 0;
};

struct OutputSnapshot {
  bool actual = false;
  uint32_t changeCount = 0;
  uint32_t lastChangeMs = 0;
};

struct Snapshot {
  InputSnapshot production;
  InputSnapshot reject;
  OutputSnapshot alarm;
};

void begin();
void update();

InputSnapshot input(InputId id);
OutputSnapshot output(OutputId id);
Snapshot snapshot();

bool ready();

}  // namespace ProductionIO
