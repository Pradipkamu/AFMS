#include "SystemHealth.h"
#include "../Communication/WiFiManager.h"
#include <ESP8266WiFi.h>

void SystemHealth::begin() {}

SystemHealthSnapshot SystemHealth::snapshot() {
  SystemHealthSnapshot value;
  value.uptimeSeconds = millis() / 1000UL;
  value.freeHeap = ESP.getFreeHeap();
  value.maxFreeBlock = ESP.getMaxFreeBlockSize();
  value.heapFragmentation = ESP.getHeapFragmentation();
  value.wifiRssi = WiFiManager::connected() ? WiFi.RSSI() : 0;
  value.resetReasonCode = static_cast<uint32_t>(ESP.getResetInfoPtr()->reason);
  return value;
}

String SystemHealth::json() {
  const SystemHealthSnapshot s = snapshot();
  String out;
  out.reserve(192);
  out += F("{\"uptime_seconds\":"); out += s.uptimeSeconds;
  out += F(",\"free_heap\":"); out += s.freeHeap;
  out += F(",\"max_free_block\":"); out += s.maxFreeBlock;
  out += F(",\"heap_fragmentation\":"); out += s.heapFragmentation;
  out += F(",\"wifi_rssi\":"); out += s.wifiRssi;
  out += F(",\"reset_reason\":"); out += s.resetReasonCode;
  out += '}';
  return out;
}
