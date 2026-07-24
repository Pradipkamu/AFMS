#include "CommunicationManager.h"
#include "../Core/Logger.h"
#include <ArduinoJson.h>
#include <LittleFS.h>
#include <cstring>

namespace {
constexpr const char *kConfigPath = "/machine.json";
bool gWebEnabled = true, gGoogleEnabled = true;
uint32_t gWebIntervalMs = 60000UL, gHeartbeatMs = 60000UL, gGoogleIntervalMs = 3600000UL;
uint32_t gMilestone = 10, gLastProduction = 0;
char gMode[10] = "HYBRID";
bool gOnStatus = true, gOnLoss = true, gOnShift = true, gGoogleBreakdown = true;
uint32_t gLastWeb = 0, gLastHeartbeat = 0, gLastGoogle = 0, gLastConfigLoad = 0;
bool gWebDue = false, gGoogleDue = false;
uint32_t gWebOk = 0, gWebFail = 0, gGoogleOk = 0, gGoogleFail = 0;

uint32_t bounded(uint32_t value, uint32_t low, uint32_t high, uint32_t fallback) { return value >= low && value <= high ? value : fallback; }
}

void CommunicationManager::reloadConfiguration() {
  File file = LittleFS.open(kConfigPath, "r");
  if (!file) { Logger::warn(F("[COMM] machine.json unavailable; using defaults")); return; }
  DynamicJsonDocument doc(12288);
  const auto error = deserializeJson(doc, file);
  file.close();
  if (error) { Logger::warn(F("[COMM] invalid machine.json; keeping active schedule")); return; }
  JsonObject web = doc["communication"]["afmsWeb"];
  JsonObject google = doc["communication"]["googleSheets"];
  gWebEnabled = web["enabled"] | true;
  gGoogleEnabled = google["enabled"] | true;
  strlcpy(gMode, web["mode"] | "HYBRID", sizeof(gMode));
  gWebIntervalMs = bounded(web["intervalSeconds"] | 60, 5, 3600, 60) * 1000UL;
  gHeartbeatMs = bounded(web["heartbeatSeconds"] | 60, 15, 3600, 60) * 1000UL;
  gMilestone = bounded(web["productionMilestone"] | 10, 1, 10000, 10);
  gGoogleIntervalMs = bounded(google["uploadIntervalSeconds"] | 3600, 60, 86400, 3600) * 1000UL;
  gOnStatus = web["sendOnStatusChange"] | true;
  gOnLoss = web["sendOnLossChange"] | true;
  gOnShift = web["sendOnShiftChange"] | true;
  gGoogleBreakdown = google["sendBreakdownImmediately"] | true;
  Logger::info(String(F("[COMM] Web=")) + (gWebEnabled ? F("ON ") : F("OFF ")) + gMode + F(" Google=") + (gGoogleEnabled ? F("ON") : F("OFF")));
}

void CommunicationManager::begin() {
  const uint32_t now = millis();
  gLastWeb = gLastHeartbeat = gLastGoogle = gLastConfigLoad = now;
  reloadConfiguration();
  Logger::info(F("[COMM] Configurable communication manager ready"));
}

void CommunicationManager::update() {
  const uint32_t now = millis();
  if (now - gLastConfigLoad >= 60000UL) { gLastConfigLoad = now; reloadConfiguration(); }
  if (gWebEnabled && strcmp(gMode, "CYCLE") != 0 && now - gLastWeb >= gWebIntervalMs) { gWebDue = true; gLastWeb = now; }
  if (gWebEnabled && now - gLastHeartbeat >= gHeartbeatMs) { gWebDue = true; gLastHeartbeat = now; }
  if (gGoogleEnabled && now - gLastGoogle >= gGoogleIntervalMs) { gGoogleDue = true; gLastGoogle = now; }
}

void CommunicationManager::notify(Trigger trigger) {
  if (gWebEnabled) {
    if (trigger == Trigger::StatusChange && gOnStatus) gWebDue = true;
    if (trigger == Trigger::LossChange && gOnLoss) gWebDue = true;
    if (trigger == Trigger::ShiftChange && gOnShift) gWebDue = true;
    if (trigger == Trigger::Heartbeat) gWebDue = true;
    if (trigger == Trigger::ProductionMilestone && strcmp(gMode, "INTERVAL") != 0) gWebDue = true;
    if (trigger == Trigger::Periodic && strcmp(gMode, "CYCLE") != 0) gWebDue = true;
  }
  if (gGoogleEnabled && (trigger == Trigger::Periodic || (trigger == Trigger::LossChange && gGoogleBreakdown))) gGoogleDue = true;
}

bool CommunicationManager::webEnabled() { return gWebEnabled; }
bool CommunicationManager::googleEnabled() { return gGoogleEnabled; }
bool CommunicationManager::webDue() { return gWebEnabled && gWebDue; }
bool CommunicationManager::googleDue() { return gGoogleEnabled && gGoogleDue; }
uint32_t CommunicationManager::webIntervalSeconds() { return gWebIntervalMs / 1000UL; }
uint32_t CommunicationManager::googleIntervalSeconds() { return gGoogleIntervalMs / 1000UL; }
uint32_t CommunicationManager::heartbeatSeconds() { return gHeartbeatMs / 1000UL; }
uint32_t CommunicationManager::productionMilestone() { return gMilestone; }
const char *CommunicationManager::webMode() { return gMode; }
void CommunicationManager::markWebComplete(bool success) { gWebDue = false; success ? ++gWebOk : ++gWebFail; }
void CommunicationManager::markGoogleComplete(bool success) { gGoogleDue = false; success ? ++gGoogleOk : ++gGoogleFail; }
uint32_t CommunicationManager::webSuccessCount() { return gWebOk; }
uint32_t CommunicationManager::webFailureCount() { return gWebFail; }
uint32_t CommunicationManager::googleSuccessCount() { return gGoogleOk; }
uint32_t CommunicationManager::googleFailureCount() { return gGoogleFail; }
