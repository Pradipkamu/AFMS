#include "CloudManager.h"
#include "HttpClientManager.h"
#include "TimeManager.h"
#include "WiFiManager.h"
#include "TelegramClient.h"
#include "../Core/Config.h"
#include "../Core/EventBus.h"
#include "../Core/LossCatalog.h"
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

String jsonEscape(const String &value) {
  return jsonEscape(value.c_str());
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
  payload.reserve(608);
  payload += F("{\"record_type\":\"event\",\"api_token\":\"");
  payload += jsonEscape(Config::apiToken());
  payload += F("\",\"machine_id\":\""); payload += jsonEscape(Config::machineId());
  payload += F("\",\"machine_name\":\""); payload += jsonEscape(Config::machineName());
  payload += F("\",\"timestamp\":\""); payload += TimeManager::iso8601();
  payload += F("\",\"event_name\":\""); payload += eventName(event.type);
  payload += F("\",\"event_value\":"); payload += event.value;
  payload += F(",\"duration_seconds\":"); payload += event.durationSeconds;
  if (event.type == EventType::LossSelected) {
    const uint16_t lossCode = static_cast<uint16_t>(event.value);
    payload += F(",\"loss_code\":"); payload += lossCode;
    payload += F(",\"loss_name\":\""); payload += jsonEscape(LossCatalog::name(lossCode)); payload += '"';
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

bool parsePendingHourlyLine(const String &line, uint32_t &notBeforeEpoch, String &payload) {
  const int separator = line.indexOf('\t');
  if (separator <= 0) return false;
  notBeforeEpoch = static_cast<uint32_t>(strtoul(line.substring(0, separator).c_str(), nullptr, 10));
  payload = line.substring(separator + 1);
  return notBeforeEpoch != 0 && payload.length() != 0;
}

bool loadFirstPendingHourly() {
  gPendingHourlyPayload = String();
  gPendingHourlyNotBeforeEpoch = 0;
  File file = LittleFS.open(kPendingHourlyPath, "r");
  if (!file || !file.available()) {
    if (file) file.close();
    return false;
  }

  const String firstLine = file.readStringUntil('\n');
  if (parsePendingHourlyLine(firstLine, gPendingHourlyNotBeforeEpoch, gPendingHourlyPayload)) {
    file.close();
    return true;
  }

  // Migrate the original two-line single-record format.
  const uint32_t legacyEpoch = static_cast<uint32_t>(strtoul(firstLine.c_str(), nullptr, 10));
  const String legacyPayload = file.readString();
  file.close();
  if (legacyEpoch == 0 || !legacyPayload.length()) {
    Logger::error(F("[GOOGLE] Invalid pending hourly records removed"));
    LittleFS.remove(kPendingHourlyPath);
    return false;
  }

  File migrated = LittleFS.open(kPendingHourlyTempPath, "w");
  if (!migrated) return false;
  migrated.print(legacyEpoch);
  migrated.print('\t');
  migrated.println(legacyPayload);
  migrated.close();
  LittleFS.remove(kPendingHourlyPath);
  if (!LittleFS.rename(kPendingHourlyTempPath, kPendingHourlyPath)) return false;
  gPendingHourlyNotBeforeEpoch = legacyEpoch;
  gPendingHourlyPayload = legacyPayload;
  Logger::info(F("[GOOGLE] Legacy pending hourly record migrated"));
  return true;
}

bool appendPendingHourly(uint32_t notBeforeEpoch, const String &payload) {
  File file = LittleFS.open(kPendingHourlyPath, "a");
  if (!file) return false;
  file.print(notBeforeEpoch);
  file.print('\t');
  const bool valid = file.println(payload) > 0;
  file.close();
  if (!valid) return false;
  if (!gPendingHourlyPayload.length()) {
    gPendingHourlyNotBeforeEpoch = notBeforeEpoch;
    gPendingHourlyPayload = payload;
  }
  return true;
}

bool removeFirstPendingHourly() {
  File source = LittleFS.open(kPendingHourlyPath, "r");
  if (!source || !source.available()) {
    if (source) source.close();
    return false;
  }
  source.readStringUntil('\n');
  File temp = LittleFS.open(kPendingHourlyTempPath, "w");
  if (!temp) {
    source.close();
    return false;
  }
  while (source.available()) temp.write(source.read());
  source.close();
  temp.close();
  LittleFS.remove(kPendingHourlyPath);
  if (!LittleFS.rename(kPendingHourlyTempPath, kPendingHourlyPath)) return false;
  loadFirstPendingHourly();
  return true;
}

void loadPendingHourly() {
  if (loadFirstPendingHourly()) Logger::info(F("[GOOGLE] Pending hourly records restored"));
}

void releasePendingHourly() {
  if (!gPendingHourlyPayload.length() || !TimeManager::synchronized()) return;
  const time_t now = TimeManager::now();
  if (now <= 0 || static_cast<uint32_t>(now) < gPendingHourlyNotBeforeEpoch) return;
  if (!queuePayload(gPendingHourlyPayload)) return;
  if (!removeFirstPendingHourly()) {
    Logger::error(F("[GOOGLE] Failed to remove released hourly record"));
    return;
  }
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

  const uint32_t periodEndEpoch = epochHour * 3600UL;
  const uint32_t notBeforeEpoch = periodEndEpoch + Config::hourlyUploadDelaySeconds();
  const String payload = buildHourlySummaryPayload(periodEndEpoch);
  if (appendPendingHourly(notBeforeEpoch, payload)) {
    gLastQueuedEpochHour = epochHour;
    Logger::info(String(F("[GOOGLE] Hourly snapshot appended; release delay ")) +
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
    if (event.type == EventType::LossSelected && event.value >= 1 && event.value <= 16) {
      TelegramClient::queueLoss(static_cast<uint16_t>(event.value), event.durationSeconds);
    }
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
