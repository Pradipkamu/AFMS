#pragma once
#include <Arduino.h>

namespace Config {
bool load();
const char *machineId();
const char *machineName();
const char *wifiSsid();
const char *wifiPassword();
const char *googleWebAppUrl();
}
