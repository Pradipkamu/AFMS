#include "ReliabilityManager.h"
#include "Logger.h"
#include <LittleFS.h>
#include <ESP8266WiFi.h>

namespace {
constexpr uint32_t kHealthIntervalMs = 1000UL;
constexpr uint32_t kLowHeapThreshold = 10000UL;
constexpr uint8_t kLowHeapLimit = 10;
constexpr size_t kMaxFaultLogBytes = 32768;

uint32_t gLastHealthMs = 0;
uint32_t gMinimumFreeHeap = UINT32_MAX;
uint32_t gLowHeapEvents = 0;
uint8_t gConsecutiveLowHeap = 0;
bool gSafeMode = false;
String gResetReason;

void appendFault(const String &message) {
  if (LittleFS.exists("/fault.log")) {
    File existing = LittleFS.open("/fault.log", "r");
    const size_t size = existing ? existing.size() : 0;
    if (existing) existing.close();
    if (size >= kMaxFaultLogBytes) {
      LittleFS.remove("/fault.log.old");
      LittleFS.rename("/fault.log", "/fault.log.old");
    }
  }

  File file = LittleFS.open("/fault.log", "a");
  if (!file) return;
  file.print(millis());
  file.print(',');
  file.println(message);
  file.close();
}
}

void ReliabilityManager::begin() {
  gResetReason = ESP.getResetReason();
  appendFault(String(F("BOOT,")) + gResetReason);
  Logger::info(String(F("Reset reason: ")) + gResetReason);
  ESP.wdtEnable(8000);
  ESP.wdtFeed();
  gMinimumFreeHeap = ESP.getFreeHeap();
}

void ReliabilityManager::update() {
  ESP.wdtFeed();
  const uint32_t now = millis();
  if (now - gLastHealthMs < kHealthIntervalMs) return;
  gLastHealthMs = now;

  const uint32_t freeHeap = ESP.getFreeHeap();
  if (freeHeap < gMinimumFreeHeap) gMinimumFreeHeap = freeHeap;

  if (freeHeap < kLowHeapThreshold) {
    ++gLowHeapEvents;
    if (gConsecutiveLowHeap < 255) ++gConsecutiveLowHeap;
    if (gConsecutiveLowHeap == 1) appendFault(String(F("LOW_HEAP,")) + freeHeap);
    if (gConsecutiveLowHeap >= kLowHeapLimit) {
      gSafeMode = true;
      appendFault(String(F("SAFE_MODE_LOW_HEAP,")) + freeHeap);
    }
  } else {
    gConsecutiveLowHeap = 0;
  }
}

bool ReliabilityManager::safeMode() { return gSafeMode; }
uint32_t ReliabilityManager::minimumFreeHeap() { return gMinimumFreeHeap; }
uint32_t ReliabilityManager::lowHeapEvents() { return gLowHeapEvents; }
const String &ReliabilityManager::lastResetReason() { return gResetReason; }
