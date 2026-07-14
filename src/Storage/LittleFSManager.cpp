#include "LittleFSManager.h"
#include "../Core/Logger.h"
#include <LittleFS.h>

namespace { bool gReady = false; }

bool LittleFSManager::begin() {
  gReady = LittleFS.begin();
  if (gReady) {
    Logger::info(F("LittleFS mounted"));
  }
  return gReady;
}

bool LittleFSManager::ready() { return gReady; }
