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
#include "../Reporting/GoogleOutboxDelivery.h"
#include "../Reporting/ReportOutboxManager.h"
#include <LittleFS.h>

namespace {
constexpr const char *kPendingHourlyPath = "/hourly.pending";
constexpr const char *kPendingHourlyTempPath = "/hourly.pending.tmp";
uint32_t gLastQueuedEpochHour = 0;
bool gHourlyScheduleInitialized = false;
String gPendingHourlyPayload;
uint32_t gPendingHourlyNotBeforeEpoch = 0;
uint32_t gEnqueueFailure = 0;
uint32_t gLossSequence = 0;

String jsonEscape(const char *value) {
  String out;
  if (!value) return out;
  while (*value) {
    if (*value == '"' || *value == '\\') out += '\\';
    out += *value++;
  }
  return out;
}

String jsonEscape(const String &value) { return jsonEscape(value.c_str()); }

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

String buildLossPayload(const Event &event) {
  const MachineSnapshot machine = MachineEngine::snapshot();
  const ShiftSnapshot shift = ShiftManager::snapshot();
  const uint16_t lossCode = static_cast<uint16_t>(event.value);
  String payload;
  payload.reserve(672);
  payload += F("{\"record_type\":\"event\",\"report_type\":\"loss_event\",\"api_token\":\"");
  payload += jsonEscape(Config::apiToken());
  payload += F("\",\"machine_id\":\""); payload += jsonEscape(Config::machineId());
  payload += F("\",\"machine_name\":\""); payload += jsonEscape(Config::machineName());
  payload += F("\",\"timestamp\":\""); payload += TimeManager::iso8601();
  payload += F("\",\"event_name\":\"loss_selected\",\"event_value\":"); payload += event.value;
  payload += F(",\"duration_seconds\":"); payload += event.durationSeconds;
  payload += F(",\"loss_code\":"); payload += lossCode;
  payload += F(",\"loss_name\":\""); payload += jsonEscape(LossCatalog::name(lossCode)); payload += '"';
  payload += F(",\"loss_duration_seconds\":"); payload += event.durationSeconds;
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

bool responseReportsFailure(const String &body) {
  if (!body.length()) return false;
  String normalized = body;
  normalized.toLowerCase();
  return normalized.indexOf(F("\"ok\":false")) >= 0 ||
         normalized.indexOf(F("\"success\":false")) >= 0 ||
         normalized.indexOf(F("\"status\":\"error\"")) >= 0 ||
         normalized.indexOf(F("\"error\":")) >= 0;
}

bool upload(const String &payload) {
  const char *url = Config::googleWebAppUrl();
  if (!url || !url[0]) {
    Logger::error(F("[GOOGLE] Web App URL is missing"));
    return false;
  }

  const HttpResult result = HttpClientManager::postJson(url, payload);
  Logger::info(String(F("[GOOGLE] HTTP status: ")) + result.code);
  if (result.body.length()) {
    String preview = result.body;
    if (preview.length() > 400) preview = preview.substring(0, 400) + F("...");
    Logger::info(String(F("[GOOGLE] Response: ")) + preview);
  }

  if (!result.success()) return false;
  if (responseReportsFailure(result.body)) {
    Logger::error(F("[GOOGLE] Application rejected payload; report retained for retry"));
    return false;
  }
  return true;
}

bool enqueueGoogle(ReportOutboxManager::ReportType type,
                   const String &payload,
                   uint32_t createdEpoch,
                   const String &reportId) {
  ReportOutboxManager::ReportRecord report;
  report.type = type;
  report.createdEpoch = createdEpoch;
  report.priority = ReportOutboxManager::defaultPriority(type);
  report.googleRequired = true;
  report.telegramRequired = false;
  report.payload = payload;
  report.reportId = reportId;
  if (ReportOutboxManager::enqueue(report)) {
    Logger::info(String(F("[GOOGLE] Outbox queued: ")) + reportId +
                 F(" type=") + ReportOutboxManager::typeName(type));
    return true;
  }
  ++gEnqueueFailure;
  Logger::error(String(F("[GOOGLE] Failed to persist report in outbox: ")) + reportId);
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
  const uint32_t legacyEpoch = static_cast<uint32_t>(strtoul(firstLine.c_str(), nullptr, 10));
  const String legacyPayload = file.readString();
  file.close();
  if (legacyEpoch == 0 || !legacyPayload.length()) {
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
  return true;
}

bool appendPendingHourly(uint32_t notBeforeEpoch, const String &payload) {
  File file = LittleFS.open(kPendingHourlyPath, "a");
  if (!file) return false;
  file.print(notBeforeEpoch);
  file.print('\t');
  const bool valid = file.println(payload) > 0;
  file.close();
  if (valid && !gPendingHourlyPayload.length()) {
    gPendingHourlyNotBeforeEpoch = notBeforeEpoch;
    gPendingHourlyPayload = payload;
  }
  return valid;
}

bool removeFirstPendingHourly() {
  File source = LittleFS.open(kPendingHourlyPath, "r");
  if (!source || !source.available()) {
    if (source) source.close();
    return false;
  }
  source.readStringUntil('\n');
  File temp = LittleFS.open(kPendingHourlyTempPath, "w");
  if (!temp) { source.close(); return false; }
  while (source.available()) temp.write(source.read());
  source.close();
  temp.close();
  LittleFS.remove(kPendingHourlyPath);
  if (!LittleFS.rename(kPendingHourlyTempPath, kPendingHourlyPath)) return false;
  loadFirstPendingHourly();
  return true;
}

void releasePendingHourly() {
  if (!gPendingHourlyPayload.length() || !TimeManager::synchronized()) return;
  const time_t now = TimeManager::now();
  if (now <= 0 || static_cast<uint32_t>(now) < gPendingHourlyNotBeforeEpoch) return;
  String id = F("HOURLY-"); id += gPendingHourlyNotBeforeEpoch;
  if (!enqueueGoogle(ReportOutboxManager::ReportType::HourlySummary,
                     gPendingHourlyPayload,
                     gPendingHourlyNotBeforeEpoch,
                     id)) return;
  if (!removeFirstPendingHourly()) Logger::error(F("[GOOGLE] Failed to remove released hourly record"));
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
    return;
  }
  if (epochHour == gLastQueuedEpochHour) return;
  const uint32_t periodEndEpoch = epochHour * 3600UL;
  const uint32_t notBeforeEpoch = periodEndEpoch + Config::hourlyUploadDelaySeconds();
  if (appendPendingHourly(notBeforeEpoch, buildHourlySummaryPayload(periodEndEpoch))) {
    gLastQueuedEpochHour = epochHour;
  } else {
    ++gEnqueueFailure;
    Logger::error(F("[GOOGLE] Failed to persist delayed hourly summary"));
  }
}
}

void CloudManager::begin() {
  HttpClientManager::begin(10000);
  TimeManager::begin();
  ReportOutboxManager::begin();
  GoogleOutboxDelivery::begin();
  TelegramClient::begin();
  loadFirstPendingHourly();
  gLastQueuedEpochHour = 0;
  gHourlyScheduleInitialized = false;
  gLossSequence = 0;
}

void CloudManager::update() {
  TimeManager::update();
  ReportOutboxManager::update();
  TelegramClient::update();

  Event event;
  while (EventBus::next(event)) {
    if (event.type != EventType::LossSelected || event.value < 1 || event.value > 16) continue;
    const time_t now = TimeManager::now();
    const uint32_t epoch = now > 0 ? static_cast<uint32_t>(now) : 0;
    String id = F("LOSS-");
    id += epoch;
    id += '-';
    id += event.value;
    id += '-';
    id += ++gLossSequence;

    Logger::info(String(F("[LOSS] Cloud event received code=")) + event.value +
                 F(" duration=") + event.durationSeconds + F(" sec"));
    enqueueGoogle(ReportOutboxManager::ReportType::LossEvent,
                  buildLossPayload(event), epoch, id);
    TelegramClient::queueLoss(static_cast<uint16_t>(event.value), event.durationSeconds);
  }

  String legacyShiftSummary;
  ShiftManager::consumeCompletedSummary(legacyShiftSummary);

  releasePendingHourly();
  scheduleHourlySummary();
  releasePendingHourly();
  GoogleOutboxDelivery::update(WiFiManager::connected(), upload);
}

void CloudManager::queueStatusNow() {
  const time_t now = TimeManager::now();
  const uint32_t epoch = now > 0 ? static_cast<uint32_t>(now) : 0;
  String id = F("STATUS-"); id += epoch; id += '-'; id += millis();
  enqueueGoogle(ReportOutboxManager::ReportType::HourlySummary,
                buildHourlySummaryPayload(epoch), epoch, id);
}

bool CloudManager::connected() {
  return GoogleOutboxDelivery::connected() && WiFiManager::connected();
}
uint32_t CloudManager::uploadSuccessCount() { return GoogleOutboxDelivery::successCount(); }
uint32_t CloudManager::uploadFailureCount() {
  return GoogleOutboxDelivery::failureCount() + gEnqueueFailure;
}
