#pragma once
#include <Arduino.h>
namespace RemoteConfigManager {
void begin();
void update();
uint32_t activeVersion();
bool usingRemoteConfig();
const char *lastError();
}
