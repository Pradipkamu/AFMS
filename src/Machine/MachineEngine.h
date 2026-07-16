#pragma once
#include "MachineState.h"

namespace MachineEngine {
void begin();
void update();
bool ready();
MachineSnapshot snapshot();
bool acknowledgeLossCode(uint16_t lossCode);
uint16_t lastAcceptedLossCode();
uint32_t lastLossDurationSeconds();
}