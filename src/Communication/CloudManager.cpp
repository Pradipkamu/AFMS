#include "CloudManager.h"
#include "HttpClientManager.h"
#include "TimeManager.h"
#include "WiFiManager.h"
#include "TelegramClient.h"
#include "../Core/Config.h"
#include "../Core/EventBus.h"
#include "../Core/Logger.h"
#include "../Machine/MachineEngine.h"
#include "../Machine/ShiftManager.h"
#include "../Storage/OfflineQueue.h"
#include <LittleFS.h>

namespace {
constexpr const char *kPendingHourlyPath = "/hourly.pending";
constexpr const char *kPendingHourlyTempPath = "/hourly.pending.tmp";
uint32_t gLastQueuedEpochHour = 0;
bool gHourlyScheduleInitialized = false;
String gPendingHourlyPayload;
uint32_t gPendingHourlyNotBeforeEpoch = 0;
uint32_t gSuccess = 0;
uint32_t gFailure = 0;
uint32_t gNextQueueRetryMs = 0;
uint8_t gQueueFailureStreak = 0;
constexpr uint32_t kQueueRetryBaseMs = 30000UL;
constexpr uint32_t kQueueRetryMaxMs = 900000UL;
constexpr uint32_t kQueueSuccessCooldownMs = 1000UL;

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
    case EventType::MachineReady: return F("machine_ready");
    case EventType::LossSelected: return F("loss_selected");
    default: return F("ignored");
  }
}

bool shouldUploadEvent(EventType type) {
  return type == EventType::MachineReady || type == EventType::LossSelected;
}

String buildHourlySummaryPayload(uint32_t periodEndEpoch) {
  const MachineSnapshot machine = MachineEngine::snapshot();
  const ShiftSnapshot shift = ShiftManager::snapshot();
  String payload;
  payload.reserve(832);
  payload += F("{\"record_type\":\"hourly_summary\",\"api_token\":\"");
  payload += jsonEscape(Config::apiToken());
  payload += F("\",\"machine_id\":\""); payload += jsonEscape(Config::machineId());
  payload += F("\",\"machine_name\":\""); payload += jsonEscape(Config::machineName());
  payload += F("\",\"timestamp\":\""); payload += TimeManager::iso8601();
  payload += F("\",\"period_end_epoch\":"); payload += periodEndEpoch;
  payload += F(",\"upload_delay_seconds\":"); payload += Config::hourlyUploadDelaySeconds();
  payload += F(",\"state\":"); payload += static_cast<uint8_t>(machine.state);
  payload += F(",\"shift\":"); payload += shift.shiftId;
  payload += F(",\"operator_id\":"); payload += shift.operatorId;
  payload += F(",\"part_number\":"); payload += shift.partNumber;
  payload += F(",\"part_name\":\""); payload += jsonEscape(shift.partName);
  payload += F("\",\"total\":"); payload += machine.totalParts;
  payload += F(",\"reject\":"); payload += machine.rejectParts;
  payload += F(",\"good\":"); payload += machine.goodParts;
  payload += F(",\"shift_production\":"); payload += shift.production;
  payload += F(",\"shift_reject\":"); payload += shift.reject;
  payload += F(",\"shift_good\":"); payload += shift.good;
  payload += F(",\"target\":"); payload += shift.targetQuantity;
  payload += F(",\"idle_seconds\":"); payload += machine.idleSeconds;
  payload += F(",\"run_seconds\":"); payload += machine.runSeconds;
  payload += F(",\"downtime_seconds\":"); payload += machine.downtimeSeconds;
  payload += F(",\"availability_permille\":"); payload += machine.availabilityPermille;
  payload += F(",\"performance_permille\":"); payload += machine.performancePermille;
  payload += F(",\"quality_permille\":"); payload += machine.qualityPermille;
  payload += F(",\"oee_permille\":"); payload += machine.oeePermille;
  payload += F(",\"alarm\":"); payload += machine.alarmActive ? F("true") : F("false");
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
  payload += F("\",\"machine_id\":\""); payload += jsonEscape(Config::machineId());
  payload += F("\",\"machine_name\":\""); payload += jsonEscape(Config::machineName());
  payload += F("\",\"timestamp\":\""); payload += TimeManager::iso8601();
  payload += F("\",\"event_name\":\""); payload += eventName(event.type);
  payload += F("\",\"event_value\":"); payload += event.value;
  payload += F(",\"duration_seconds\":"); payload += event.durationSeconds;
  if (event.type == EventType::LossSelected) {
    payload += F(",\"loss_code\":"); payload += event.value;
    payload += F(",\"loss_duration_seconds\":"); payload += event.durationSeconds;
  }
  payload += F(",\"state\":"); payload += static_cast<uint8_t>(machine.state);
  payload += F(",\"shift\":"); payload += shift.shiftId;
  payload += F(",\"operator_id\":"); payload += shift.operatorId;
  payload += F(",\"part_number\":"); payload += shift.partNumber;
  payload += F(",\"part_name\":\""); payload += jsonEscape(shift.partName);
  payload += F("\",\"total\":"); payload += machine.totalParts;
  payload += F(",\"reject\":"); payload += machine.rejectParts;
  payload += F(",\"good\":"); payload += machine.goodParts;
  payload += F(",\"alarm\":"); payload += machine.alarmActive ? F("true") : F("false");
  payload += F("}");
  return payload;
}

bool upload(const String &payload) {
  const char *url = Config::googleWebAppUrl();
  if (!url || !url[0]) return false;
  const HttpResult result = HttpClientManager::postJson(url, payload);
  Logger::info(String(F("[GOOGLE] HTTP status: ")) + result.code);
  if (result.success()) { ++gSuccess; return true; }
  ++gFailure;
  return false;
}

bool queuePayload(const String &payload) {
  if (OfflineQueue::push(payload)) return true;
  Logger::error(F("[GOOGLE] Failed to store record in offline queue"));
  ++gFailure;
  return false;
}

bool persistPendingHourly(uint32_t notBeforeEpoch, const String &payload) {
  File file = LittleFS.open(kPendingHourlyTempPath, "w");
  if (!file) return false;
  file.println(notBeforeEpoch);
  file.print(payload);
  file.flush();
  const bool valid = file.getWriteError() == 0;
  file.close();
  if (!valid) {
    LittleFS.remove(kPendingHourlyTempPath);
    return false;
  }
  LittleFS.remove(kPendingHourlyPath);
  if (!LittleFS.rename(kPendingHourlyTempPath, kPendingHourlyPath)) {
    LittleFS.remove(kPendingHourlyTempPath);
    return false;
  }
  gPendingHourlyNotBeforeEpoch = notBeforeEpoch;
  gPendingHourlyPayload = payload;
  return true;
}

void loadPendingHourly() {
  gPendingHourlyPayload = String();
  gPendingHourlyNotBeforeEpoch = 0;
  File file = LittleFS.open(kPendingHourlyPath, "r");
  if (!file) return;
  const String epochLine = file.readStringUntil('\n');
  gPendingHourlyNotBeforeEpoch = static_cast<uint32_t>(strtoul(epochLine.c_str(), nullptr, 10));
  gPendingHourlyPayload = file.readString();
  file.close();
  if (gPendingHourlyNotBeforeEpoch == 0 || !gPendingHourlyPayload.length()) {
    Logger::error(F("[GOOGLE] Invalid pending hourly record removed"));
    LittleFS.remove(kPendingHourlyPath);
    gPendingHourlyNotBeforeEpoch = 0;
    gPendingHourlyPayload = String();
    return;
  }
  Logger::info(F("[GOOGLE] Pending hourly record restored"));
}

void releasePendingHourly() {
  if (!gPendingHourlyPayload.length() || !TimeManager::synchronized()) return;
  const time_t now = TimeManager::now();
  if (now <= 0 || static_cast<uint32_t>(now) < gPendingHourlyNotBeforeEpoch) return;
  if (!queuePayload(gPendingHourlyPayload)) return;
  LittleFS.remove(kPendingHourlyPath);
  gPendingHourlyPayload = String();
  gPendingHourlyNotBeforeEpoch = 0;
  Logger::info(F("[GOOGLE] Delayed hourly summary released to queue"));
}

void scheduleHourlySummary() {
  if (!TimeManager::synchronized()) return;

  const time_t now = TimeManager::now();
  if (now <= 0) return;
  const uint32_t epochNow = static_cast<uint32_t>(now);
  const uint32_t epochHour = epochNow / 3600UL;

  if (!gHourlyScheduleInitialized) {
    gLastQueuedEpochHour = epochHour;
    gHourlyScheduleInitialized = true;
    Logger::info(F("[GOOGLE] Hourly scheduler synchronized"));
    return;
  }

  if (epochHour == gLastQueuedEpochHour) return;
  if (gPendingHourlyPayload.length()) {
    Logger::warn(F("[GOOGLE] Previous hourly summary still pending; new snapshot deferred"));
    return;
  }

  const uint32_t periodEndEpoch = epochHour * 3600UL;
  const uint32_t notBeforeEpoch = periodEndEpoch + Config::hourlyUploadDelaySeconds();
  const String payload = buildHourlySummaryPayload(periodEndEpoch);
  if (persistPendingHourly(notBeforeEpoch, payload)) {
    gLastQueuedEpochHour = epochHour;
    Logger::info(String(F("[GOOGLE] Hourly snapshot saved; release delay ")) +
                 Config::hourlyUploadDelaySeconds() + F(" sec"));
  } else {
    Logger::error(F("[GOOGLE] Failed to persist delayed hourly summary"));
    ++gFailure;
  }
}

bool queueRetryDue(uint32_t nowMs) {
  return gNextQueueRetryMs == 0 ||
         static_cast<int32_t>(nowMs - gNextQueueRetryMs) >= 0;
}

uint32_t queueRetryDelayMs() {
  const uint8_t exponent = gQueueFailureStreak > 5 ? 5 : gQueueFailureStreak;
  uint32_t delayMs = kQueueRetryBaseMs << exponent;
  if (delayMs > kQueueRetryMaxMs) delayMs = kQueueRetryMaxMs;
  return delayMs;
}

void scheduleQueueRetry(uint32_t nowMs) {
  if (gQueueFailureStreak < 255) ++gQueueFailureStreak;
  const uint32_t delayMs = queueRetryDelayMs();
  gNextQueueRetryMs = nowMs + delayMs;
  Logger::warn(String(F("[GOOGLE] Queue retry in ")) + (delayMs / 1000UL) + F(" seconds"));
}

void processQueuedUpload() {
  if (!WiFiManager::connected()) return;

  String queued;
  if (!OfflineQueue::peek(queued)) {
    gQueueFailureStreak = 0;
    gNextQueueRetryMs = 0;
    return;
  }

  const uint32_t nowMs = millis();
  if (!queueRetryDue(nowMs)) return;

  if (upload(queued)) {
    OfflineQueue::pop();
    gQueueFailureStreak = 0;
    gNextQueueRetryMs = nowMs + kQueueSuccessCooldownMs;
    Logger::info(F("[GOOGLE] Queued record uploaded"));
  } else {
    scheduleQueueRetry(nowMs);
  }
}
}

void CloudManager::begin() {
  HttpClientManager::begin(10000);
  TimeManager::begin();
  OfflineQueue::begin();
  TelegramClient::begin();
  loadPendingHourly();
  gLastQueuedEpochHour = 0;
  gHourlyScheduleInitialized = false;
  gNextQueueRetryMs = 0;
  gQueueFailureStreak = 0;
}

void CloudManager::update() {
  TimeManager::update();
  TelegramClient::update();

  Event event;
  while (EventBus::next(event)) {
    if (!shouldUploadEvent(event.type)) continue;
    queuePayload(buildEventPayload(event));
    if (event.type == EventType::MachineReady) TelegramClient::sendMachineReady();
    else if (event.type == EventType::LossSelected && event.value >= 1 && event.value <= 16)
      TelegramClient::sendLoss(static_cast<uint16_t>(event.value), event.durationSeconds);
  }

  String shiftSummary;
  if (ShiftManager::consumeCompletedSummary(shiftSummary)) queuePayload(shiftSummary);

  releasePendingHourly();
  scheduleHourlySummary();
  releasePendingHourly();
  processQueuedUpload();
}

void CloudManager::queueStatusNow() {
  const time_t now = TimeManager::now();
  queuePayload(buildHourlySummaryPayload(now > 0 ? static_cast<uint32_t>(now) : 0));
}
uint32_t CloudManager::uploadSuccessCount() { return gSuccess; }
uint32_t CloudManager::uploadFailureCount() { return gFailure; }