#pragma once
#include "MachineState.h"

namespace MachineEngine {
void begin();
void update();
bool ready();
MachineSnapshot snapshot();
void acknowledgeLossCode(uint16_t lossCode);
}
