#include "CloudManager.h"
#include "HttpClientManager.h"
#include "TimeManager.h"
#include "WiFiManager.h"
#include "../Core/Config.h"
#include "../Core/Logger.h"
#include "../Machine/MachineEngine.h"
#include "../Storage/OfflineQueue.h"

namespace {
uint32_t gLastHeartbeatMs = 0;
uint32_t gSuccess = 0;
uint32_t gFailure = 0;
constexpr uint32_t kHeartbeatMs = 60000UL;

String jsonEscape(const char *value) {
  String out;
  if (!value) return out;
  while (*value) {
    if (*value == '"' || *value == '\\') out += '\\';
    out += *value++;
  }
  return out;
}

String buildStatusPayload() {
  const MachineSnapshot snapshot = MachineEngine::snapshot();
  String payload;
  payload.reserve(448);
  payload += F("{\"machine_id\":\"");
  payload += jsonEscape(Config::machineId());
  payload += F("\",\"machine_name\":\"");
  payload += jsonEscape(Config::machineName());
  payload += F("\",\"timestamp\":\"");
  payload += TimeManager::iso8601();
  payload += F("\",\"state\":");
  payload += static_cast<uint8_t>(snapshot.state);
  payload += F(",\"total\":");
  payload += snapshot.totalParts;
  payload += F(",\"reject\":");
  payload += snapshot.rejectParts;
  payload += F(",\"good\":");
  payload += snapshot.goodParts;
  payload += F(",\"target\":");
  payload += snapshot.targetQuantity;
  payload += F(",\"idle_seconds\":");
  payload += snapshot.idleSeconds;
  payload += F(",\"run_seconds\":");
  payload += snapshot.runSeconds;
  payload += F(",\"downtime_seconds\":");
  payload += snapshot.downtimeSeconds;
  payload += F(",\"availability_permille\":");
  payload += snapshot.availabilityPermille;
  payload += F(",\"performance_permille\":");
  payload += snapshot.performancePermille;
  payload += F(",\"quality_permille\":");
  payload += snapshot.qualityPermille;
  payload += F(",\"oee_permille\":");
  payload += snapshot.oeePermille;
  payload += F(",\"alarm\":");
  payload += snapshot.alarmActive ? F("true") : F("false");
  payload += F("}");
  return payload;
}

bool upload(const String &payload) {
  const char *url = Config::googleWebAppUrl();
  if (!url || !url[0]) return false;
  const HttpResult result = HttpClientManager::postJson(url, payload);
  if (result.success()) {
    ++gSuccess;
    return true;
  }
  ++gFailure;
  return false;
}
}

void CloudManager::begin() {
  HttpClientManager::begin(10000);
  TimeManager::begin();
  OfflineQueue::begin();
  gLastHeartbeatMs = millis();
}

void CloudManager::update() {
  TimeManager::update();
  if (!WiFiManager::connected()) return;

  String queued;
  if (OfflineQueue::peek(queued)) {
    if (upload(queued)) OfflineQueue::pop();
    return;
  }

  if (millis() - gLastHeartbeatMs >= kHeartbeatMs) {
    gLastHeartbeatMs = millis();
    const String payload = buildStatusPayload();
    if (!upload(payload)) OfflineQueue::push(payload);
  }
}

void CloudManager::queueStatusNow() {
  const String payload = buildStatusPayload();
  if (!WiFiManager::connected() || !upload(payload)) OfflineQueue::push(payload);
}

uint32_t CloudManager::uploadSuccessCount() { return gSuccess; }
uint32_t CloudManager::uploadFailureCount() { return gFailure; }
