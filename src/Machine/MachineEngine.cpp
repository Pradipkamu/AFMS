#include "MachineEngine.h"
#include "../Core/EventBus.h"
#include "../Core/Logger.h"

namespace { bool gReady = false; }

void MachineEngine::begin() {
  gReady = true;
  EventBus::publish(EventType::MachineReady);
  Logger::info(F("Machine engine ready"));
}

void MachineEngine::update() {
  // Commit 0001 intentionally limits the machine engine to lifecycle handling.
  // Pulse counting and OEE logic are added in Commit 0002.
}

bool MachineEngine::ready() { return gReady; }
