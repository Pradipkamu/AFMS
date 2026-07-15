#include "CloudManager.h"
#include "HttpClientManager.h"
#include "TimeManager.h"
#include "WiFiManager.h"
#include "../Core/Config.h"
#include "../Core/EventBus.h"
#include "../Core/Logger.h"
#include "../Machine/MachineEngine.h"
#include "../Machine/ShiftManager.h"
#include "../Storage/OfflineQueue.h"

namespace {
uint32_t gLastHourlySummaryMs = 0;
uint32_t gSuccess = 0;
uint32_t gFailure = 0;
constexpr uint32_t kHourlySummaryMs = 3600000UL;

String jsonEscape(const char *value) {
  String out;
  if (!value) return out;
  while (*value) {
    if (*value == '"' || *value == '\\') out += '\\';
    out += *value++;
  }
  return out;
}

const __FlashStringHelper *eventName(EventType type) {
  switch (type) {
    case EventType::SystemBoot: return F("system_boot");
    case EventType::WifiConnected: return F("wifi_connected");
    case EventType::WifiDisconnected: return F("wifi_disconnected");
    case EventType::MachineReady: return F("machine_ready");
    case EventType::IdleStarted: return F("idle_started");
    case EventType::IdleEnded: return F("idle_ended");
    case EventType::AlarmActivated: return F("alarm_activated");
    case EventType::AlarmCleared: return F("alarm_cleared");
    case EventType::LossSelected: return F("loss_selected");
    case EventType::ProductionPulse: return F("production_pulse");
    case EventType::RejectPulse: return F("reject_pulse");
  }
  return F("unknown");
}

bool shouldUploadEvent(EventType type) {
  return type != EventType::ProductionPulse && type != EventType::RejectPulse;
}

String buildHourlySummaryPayload() {
  const MachineSnapshot machine = MachineEngine::snapshot();
  const ShiftSnapshot shift = ShiftManager::snapshot();
  String payload;
  payload.reserve(768);
  payload += F("{\"record_type\":\"hourly_summary\",\"api_token\":\"");
  payload += jsonEscape(Config::apiToken());
  payload += F("\",\"machine_id\":\"");
  payload += jsonEscape(Config::machineId());
  payload += F("\",\"machine_name\":\"");
  payload += jsonEscape(Config::machineName());
  payload += F("\",\"timestamp\":\"");
  payload += TimeManager::iso8601();
  payload += F("\",\"state\":");
  payload += static_cast<uint8_t>(machine.state);
  payload += F(",\"shift\":");
  payload += shift.shiftId;
  payload += F(",\"operator_id\":");
  payload += shift.operatorId;
  payload += F(",\"part_number\":");
  payload += shift.partNumber;
  payload += F(",\"part_name\":\"");
  payload += jsonEscape(shift.partName);
  payload += F("\",\"total\":");
  payload += machine.totalParts;
  payload += F(",\"reject\":");
  payload += machine.rejectParts;
  payload += F(",\"good\":");
  payload += machine.goodParts;
  payload += F(",\"shift_production\":");
  payload += shift.production;
  payload += F(",\"shift_reject\":");
  payload += shift.reject;
  payload += F(",\"shift_good\":");
  payload += shift.good;
  payload += F(",\"target\":");
  payload += shift.targetQuantity;
  payload += F(",\"idle_seconds\":");
  payload += machine.idleSeconds;
  payload += F(",\"run_seconds\":");
  payload += machine.runSeconds;
  payload += F(",\"downtime_seconds\":");
  payload += machine.downtimeSeconds;
  payload += F(",\"availability_permille\":");
  payload += machine.availabilityPermille;
  payload += F(",\"performance_permille\":");
  payload += machine.performancePermille;
  payload += F(",\"quality_permille\":");
  payload += machine.qualityPermille;
  payload += F(",\"oee_permille\":");
  payload += machine.oeePermille;
  payload += F(",\"alarm\":");
  payload += machine.alarmActive ? F("true") : F("false");
  payload += F("}");
  return payload;
}

String buildEventPayload(const Event &event) {
  const MachineSnapshot machine = MachineEngine::snapshot();
  const ShiftSnapshot shift = ShiftManager::snapshot();
  String payload;
  payload.reserve(544);
  payload += F("{\"record_type\":\"event\",\"api_token\":\"");
  payload += jsonEscape(Config::apiToken());
  payload += F("\",\"machine_id\":\"");
  payload += jsonEscape(Config::machineId());
  payload += F("\",\"machine_name\":\"");
  payload += jsonEscape(Config::machineName());
  payload += F("\",\"timestamp\":\"");
  payload += TimeManager::iso8601();
  payload += F("\",\"event_name\":\"");
  payload += eventName(event.type);
  payload += F("\",\"event_value\":");
  payload += event.value;
  payload += F(",\"duration_seconds\":");
  payload += event.durationSeconds;
  if (event.type == EventType::LossSelected) {
    payload += F(",\"loss_code\":");
    payload += event.value;
    payload += F(",\"loss_duration_seconds\":");
    payload += event.durationSeconds;
  }
  payload += F(",\"state\":");
  payload += static_cast<uint8_t>(machine.state);
  payload += F(",\"shift\":");
  payload += shift.shiftId;
  payload += F(",\"operator_id\":");
  payload += shift.operatorId;
  payload += F(",\"part_number\":");
  payload += shift.partNumber;
  payload += F(",\"part_name\":\"");
  payload += jsonEscape(shift.partName);
  payload += F("\",\"total\":");
  payload += machine.totalParts;
  payload += F(",\"reject\":");
  payload += machine.rejectParts;
  payload += F(",\"good\":");
  payload += machine.goodParts;
  payload += F(",\"alarm\":");
  payload += machine.alarmActive ? F("true") : F("false");
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

void deliverOrQueue(const String &payload) {
  if (!WiFiManager::connected() || !upload(payload)) OfflineQueue::push(payload);
}
}

void CloudManager::begin() {
  HttpClientManager::begin(10000);
  TimeManager::begin();
  OfflineQueue::begin();
  gLastHourlySummaryMs = millis();
}

void CloudManager::update() {
  TimeManager::update();

  Event event;
  while (EventBus::next(event)) {
    if (shouldUploadEvent(event.type)) deliverOrQueue(buildEventPayload(event));
  }

  String shiftSummary;
  if (ShiftManager::consumeCompletedSummary(shiftSummary)) {
    deliverOrQueue(shiftSummary);
  }

  if (!WiFiManager::connected()) return;

  String queued;
  if (OfflineQueue::peek(queued)) {
    if (upload(queued)) OfflineQueue::pop();
    return;
  }

  if (millis() - gLastHourlySummaryMs >= kHourlySummaryMs) {
    gLastHourlySummaryMs = millis();
    deliverOrQueue(buildHourlySummaryPayload());
  }
}

void CloudManager::queueStatusNow() { deliverOrQueue(buildHourlySummaryPayload()); }
uint32_t CloudManager::uploadSuccessCount() { return gSuccess; }
uint32_t CloudManager::uploadFailureCount() { return gFailure; }
