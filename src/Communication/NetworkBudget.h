#pragma once
#include <Arduino.h>

namespace NetworkBudget {
// Reset once at the beginning of every main-loop pass.
void beginLoop();
// Returns true only for the first network transaction attempted in the pass.
bool acquire();
bool used();
uint32_t deferredCount();
}
