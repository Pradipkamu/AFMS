#pragma once
#include <Arduino.h>

struct SystemHealthSnapshot {
  uint32_t uptimeSeconds;
  uint32_t freeHeap;
  uint32_t maxFreeBlock;
  uint8_t heapFragmentation;
  int32_t wifiRssi;
  uint32_t resetReasonCode;
};

namespace SystemHealth {
void begin();
SystemHealthSnapshot snapshot();
String json();
}
