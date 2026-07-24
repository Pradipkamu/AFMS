#include "NetworkBudget.h"

namespace {
bool gUsed = false;
uint32_t gDeferred = 0;
}

void NetworkBudget::beginLoop() { gUsed = false; }

bool NetworkBudget::acquire() {
  if (gUsed) {
    ++gDeferred;
    return false;
  }
  gUsed = true;
  return true;
}

bool NetworkBudget::used() { return gUsed; }
uint32_t NetworkBudget::deferredCount() { return gDeferred; }
