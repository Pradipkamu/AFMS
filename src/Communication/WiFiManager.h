#pragma once
#include <Arduino.h>

namespace WiFiManager {
void begin(const char *ssid, const char *password);
void update();
bool connected();
int32_t rssi();
uint32_t reconnectCount();
}
