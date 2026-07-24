#include "CloudManager.h"
#include "HttpClientManager.h"
#include "TimeManager.h"
#include "WiFiManager.h"
#include "TelegramClient.h"
#include "../Core/Config.h"
#include "../Core/EventBus.h"
#include "../Core/LossCatalog.h"
#include "../Core/Logger.h"
#include "../Core/ReliabilityManager.h"
#include "../Machine/MachineEngine.h"
#include "../Machine/ShiftManager.h"
#include "../Machine/OEEManager.h"
#include "../Storage/ShiftCsvManager.h"
#include "../Reporting/ReportOutboxManager.h"
#include "../Reporting/ReportingCsvManager.h"
#include <ArduinoJson.h>
#include <ESP8266WiFi.h>
#include <WiFiClientSecureBearSSL.h>
#include <LittleFS.h>
#include <time.h>

namespace {
constexpr const char *kHourStatePath = "/reporting_hour.state";
constexpr const char *kMonthStatePath = "/reporting_month.state";
constexpr uint32_t kRetryBaseMs = 30000UL;
constexpr uint32_t kRetryMaxMs = 900000UL;
constexpr uint32_t kSuccessCooldownMs = 1000UL;
uint32_t gSuccess = 0;
uint32_t gFailure = 0;
bool gConnected = false;
uint32_t gNextRetryMs = 0;
uint8_t gFailureStreak = 0;
uint32_t gLastCompletedHour = 0;
uint32_t gLastMonthKey = 0;
char gTelegramBotToken[96] = "";
char gTelegramChatId[32] = "";

String jsonEscape(const char *value) {
  String out;
  if (!value) return out;
  while (*value) {
    if (*value == '"' || *value == '\\') out += '\\';
    if (*value == '\n') out += F("\\n");
    else if (*value != '\r') out += *value;
    ++value;
  }
  return out;
}

String isoAt(uint32_t epoch) {
  if (!epoch) return TimeManager::iso8601();
  time_t raw = static_cast<time_t>(epoch);
  struct tm value;
  if (!localtime_r(&raw, &value)) return TimeManager::iso8601();
  char text[28];
  strftime(text, sizeof(text), "%Y-%m-%dT%H:%M:%S%z", &value);
  return String(text);
}

uint32_t eventEpoch(const Event &event) {
  const time_t now = TimeManager::now();
  if (now <= 0) return 0;
  const uint32_t elapsedSeconds = (millis() - event.timestampMs) / 1000UL;
  const uint32_t epochNow = static_cast<uint32_t>(now);
  return elapsedSeconds <= epochNow ? epochNow - elapsedSeconds : epochNow;
}

String buildSnapshotPayload(const char *type, uint32_t epoch, bool recovered = false) {
  const MachineSnapshot machine = MachineEngine::snapshot();
  const ShiftSnapshot shift = ShiftManager::snapshot();
  String payload;
  payload.reserve(900);
  payload += F("{\"record_type\":\""); payload += type;
  payload += F("\",\"api_token\":\""); payload += jsonEscape(Config::apiToken());
  payload += F("\",\"machine_id\":\""); payload += jsonEscape(Config::machineId());
  payload += F("\",\"machine_name\":\""); payload += jsonEscape(Config::machineName());
  payload += F("\",\"timestamp\":\""); payload += isoAt(epoch);
  payload += F("\",\"created_epoch\":"); payload += epoch;
  payload += F(",\"recovered\":"); payload += recovered ? F("true") : F("false");
  payload += F(",\"state\":"); payload += static_cast<uint8_t>(machine.state);
  payload += F(",\"shift\":"); payload += shift.shiftId;
  payload += F(",\"operator_id\":"); payload += shift.operatorId;
  payload += F(",\"part_number\":"); payload += shift.partNumber;
  payload += F(",\"part_name\":\""); payload += jsonEscape(shift.partName);
  payload += F("\",\"target\":"); payload += shift.targetQuantity;
  payload += F(",\"total\":"); payload += machine.totalParts;
  payload += F(",\"reject\":"); payload += machine.rejectParts;
  payload += F(",\"good\":"); payload += machine.goodParts;
  payload += F(",\"idle_seconds\":"); payload += machine.idleSeconds;
  payload += F(",\"run_seconds\":"); payload += machine.runSeconds;
  payload += F(",\"downtime_seconds\":"); payload += machine.downtimeSeconds;
  payload += F(",\"availability_permille\":"); payload += machine.availabilityPermille;
  payload += F(",\"performance_permille\":"); payload += machine.performancePermille;
  payload += F(",\"quality_permille\":"); payload += machine.qualityPermille;
  payload += F(",\"oee_permille\":"); payload += machine.oeePermille;
  payload += F(",\"alarm\":"); payload += machine.alarmActive ? F("true") : F("false");
  payload += '}';
  return payload;
}

String buildLossPayload(uint32_t epoch, uint16_t lossCode, uint32_t durationSeconds,
                        const MachineSnapshot &machine, const ShiftSnapshot &shift) {
  String payload;
  payload.reserve(850);
  payload += F("{\"record_type\":\"loss\",\"api_token\":\"");
  payload += jsonEscape(Config::apiToken());
  payload += F("\",\"machine_id\":\""); payload += jsonEscape(Config::machineId());
  payload += F("\",\"machine_name\":\""); payload += jsonEscape(Config::machineName());
  payload += F("\",\"timestamp\":\""); payload += isoAt(epoch);
  payload += F("\",\"created_epoch\":"); payload += epoch;
  payload += F(",\"loss_code\":"); payload += lossCode;
  payload += F(",\"loss_name\":\""); payload += jsonEscape(LossCatalog::name(lossCode).c_str());
  payload += F("\",\"loss_duration_seconds\":"); payload += durationSeconds;
  payload += F(",\"state\":"); payload += static_cast<uint8_t>(machine.state);
  payload += F(",\"shift\":"); payload += shift.shiftId;
  payload += F(",\"operator_id\":"); payload += shift.operatorId;
  payload += F(",\"part_number\":"); payload += shift.partNumber;
  payload += F(",\"part_name\":\""); payload += jsonEscape(shift.partName);
  payload += F("\",\"total\":"); payload += machine.totalParts;
  payload += F(",\"reject\":"); payload += machine.rejectParts;
  payload += F(",\"good\":"); payload += machine.goodParts;
  payload += '}';
  return payload;
}

bool googleConfigured() {
  const char *url = Config::googleWebAppUrl();
  return url && url[0];
}

bool uploadGoogle(const String &payload) {
  if (!googleConfigured() || !WiFiManager::connected()) return false;
  const HttpResult result = HttpClientManager::postJson(Config::googleWebAppUrl(), payload);
  Logger::info(String(F("[GOOGLE] HTTP status: ")) + result.code);
  if (result.success()) {
    ++gSuccess;
    gConnected = true;
    return true;
  }
  ++gFailure;
  gConnected = false;
  return false;
}

void loadTelegramConfig() {
  File file = LittleFS.open("/machine.json", "r");
  if (!file) return;
  DynamicJsonDocument document(6144);
  const DeserializationError error = deserializeJson(document, file);
  file.close();
  if (error) return;
  strlcpy(gTelegramBotToken, document["telegram_bot_token"] | "", sizeof(gTelegramBotToken));
  strlcpy(gTelegramChatId, document["telegram_chat_id"] | "", sizeof(gTelegramChatId));
}

bool sendTelegramDocument(const String &path) {
  if (!gTelegramBotToken[0] || !gTelegramChatId[0] || !WiFiManager::connected()) return false;
  File file = LittleFS.open(path, "r");
  if (!file) return false;
  String filename = path;
  const int slash = filename.lastIndexOf('/');
  if (slash >= 0) filename = filename.substring(slash + 1);
  const String boundary = F("----AFMS311OutboxBoundary");
  String prefix;
  prefix += F("--"); prefix += boundary; prefix += F("\r\nContent-Disposition: form-data; name=\"chat_id\"\r\n\r\n");
  prefix += gTelegramChatId;
  prefix += F("\r\n--"); prefix += boundary; prefix += F("\r\nContent-Disposition: form-data; name=\"document\"; filename=\"");
  prefix += filename;
  prefix += F("\"\r\nContent-Type: text/csv\r\n\r\n");
  String suffix = F("\r\n--"); suffix += boundary; suffix += F("--\r\n");

  BearSSL::WiFiClientSecure client;
  client.setInsecure();
  client.setTimeout(15000);
  if (!client.connect("api.telegram.org", 443)) { file.close(); return false; }
  client.print(F("POST /bot")); client.print(gTelegramBotToken); client.println(F("/sendDocument HTTP/1.1"));
  client.println(F("Host: api.telegram.org"));
  client.print(F("Content-Type: multipart/form-data; boundary=")); client.println(boundary);
  client.print(F("Content-Length: ")); client.println(prefix.length() + file.size() + suffix.length());
  client.println(F("Connection: close\r\n"));
  client.print(prefix);
  uint8_t buffer[512];
  while (file.available()) {
    const size_t count = file.read(buffer, sizeof(buffer));
    if (count) client.write(buffer, count);
    yield();
  }
  file.close();
  client.print(suffix);
  const uint32_t started = millis();
  while (!client.available() && client.connected() && millis() - started < 15000UL) yield();
  String response;
  while (client.available()) {
    response += static_cast<char>(client.read());
    if (response.length() > 1024) response.remove(0, 256);
  }
  client.stop();
  return response.indexOf(F(" 200 ")) >= 0 && response.indexOf(F("\"ok\":true")) >= 0;
}

uint32_t loadNumber(const char *path) {
  File file = LittleFS.open(path, "r");
  if (!file) return 0;
  const uint32_t value = static_cast<uint32_t>(file.parseInt());
  file.close();
  return value;
}

void saveNumber(const char *path, uint32_t value) {
  File file = LittleFS.open(path, "w");
  if (!file) return;
  file.print(value);
  file.flush();
  file.close();
}

uint32_t monthKey(uint32_t epoch) {
  time_t raw = static_cast<time_t>(epoch);
  struct tm value;
  if (!epoch || !localtime_r(&raw, &value)) return 0;
  return static_cast<uint32_t>(value.tm_year + 1900) * 100UL + static_cast<uint32_t>(value.tm_mon + 1);
}

String monthlyPath(const char *prefix, uint32_t key) {
  char path[48];
  snprintf(path, sizeof(path), "/reports/%s_%04lu-%02lu.csv", prefix,
           static_cast<unsigned long>(key / 100UL), static_cast<unsigned long>(key % 100UL));
  return String(path);
}

void enqueueMonthEnd(uint32_t previousMonth, uint32_t epoch) {
  const char *prefixes[] = {"shift", "hourly", "loss", "statistics"};
  for (uint8_t i = 0; i < 4; ++i) {
    const String path = monthlyPath(prefixes[i], previousMonth);
    if (!LittleFS.exists(path)) continue;
    ReportOutboxManager::enqueue("monthly_csv", 100, epoch, String(), false,
                                 TelegramClient::configured(), ReportTelegramKind::Document,
                                 0, 0, path);
  }
}

void processEvents() {
  Event event;
  while (EventBus::next(event)) {
    if (event.type == EventType::LossSelected && event.value >= 1 && event.value <= 16) {
      const uint32_t epoch = eventEpoch(event);
      const uint16_t code = static_cast<uint16_t>(event.value);
      const MachineSnapshot machine = MachineEngine::snapshot();
      const ShiftSnapshot shift = ShiftManager::snapshot();
      ReportingCsvManager::appendLoss(epoch, code, LossCatalog::name(code).c_str(),
                                      event.durationSeconds, machine, shift);
      ReportOutboxManager::enqueue("loss", 80, epoch,
          buildLossPayload(epoch, code, event.durationSeconds, machine, shift),
          googleConfigured(), TelegramClient::configured(), ReportTelegramKind::Loss,
          code, event.durationSeconds, ReportingCsvManager::lossPath(epoch));
    } else if (event.type == EventType::MachineReady) {
      const uint32_t epoch = eventEpoch(event);
      ReportOutboxManager::enqueue("machine_ready", 20, epoch,
          buildSnapshotPayload("machine_ready", epoch), googleConfigured(), false);
    }
  }
}

void processShiftReports() {
  String summary;
  if (ShiftManager::consumeCompletedSummary(summary)) {
    const uint32_t epoch = static_cast<uint32_t>(TimeManager::now());
    ReportOutboxManager::enqueue("shift", 110, epoch, summary, googleConfigured(), false);
  }
  String csvPath;
  if (ShiftCsvManager::consumeDailyReportReady(csvPath)) {
    const uint32_t epoch = static_cast<uint32_t>(TimeManager::now());
    ReportOutboxManager::enqueue("shift_csv", 110, epoch, String(), false,
                                 TelegramClient::configured(), ReportTelegramKind::Document,
                                 0, 0, csvPath);
  }
}

void processHourly() {
  if (!TimeManager::synchronized()) return;
  const time_t nowRaw = TimeManager::now();
  if (nowRaw <= 0) return;
  const uint32_t now = static_cast<uint32_t>(nowRaw);
  const uint32_t currentHour = now / 3600UL;
  if (gLastCompletedHour == 0) {
    gLastCompletedHour = currentHour;
    saveNumber(kHourStatePath, gLastCompletedHour);
    return;
  }
  while (gLastCompletedHour < currentHour) {
    ++gLastCompletedHour;
    const uint32_t periodEnd = gLastCompletedHour * 3600UL;
    const bool recovered = gLastCompletedHour < currentHour;
    const MachineSnapshot machine = MachineEngine::snapshot();
    const ShiftSnapshot shift = ShiftManager::snapshot();
    const OEESnapshot oee = OEEManager::snapshot();
    ReportingCsvManager::appendHourly(periodEnd, machine, shift, oee, recovered);
    ReportingCsvManager::writeStatistics(periodEnd, machine, shift, oee);
    ReportOutboxManager::enqueue(recovered ? "hourly_recovered" : "hourly", 40,
        periodEnd, buildSnapshotPayload(recovered ? "hourly_recovered" : "hourly_summary", periodEnd, recovered),
        googleConfigured(), false, ReportTelegramKind::None, 0, 0,
        ReportingCsvManager::hourlyPath(periodEnd));
    saveNumber(kHourStatePath, gLastCompletedHour);
    yield();
  }

  const uint32_t currentMonth = monthKey(now);
  if (gLastMonthKey == 0) {
    gLastMonthKey = currentMonth;
    saveNumber(kMonthStatePath, gLastMonthKey);
  } else if (currentMonth != 0 && currentMonth != gLastMonthKey) {
    enqueueMonthEnd(gLastMonthKey, now);
    gLastMonthKey = currentMonth;
    saveNumber(kMonthStatePath, gLastMonthKey);
  }
}

uint32_t retryDelay() {
  const uint8_t exponent = gFailureStreak > 5 ? 5 : gFailureStreak;
  uint32_t value = kRetryBaseMs << exponent;
  return value > kRetryMaxMs ? kRetryMaxMs : value;
}

void scheduleRetry() {
  if (gFailureStreak < 255) ++gFailureStreak;
  gNextRetryMs = millis() + retryDelay();
}

void dispatchOne() {
  if (ReliabilityManager::safeMode() || !WiFiManager::connected()) return;
  if (gNextRetryMs && static_cast<int32_t>(millis() - gNextRetryMs) < 0) return;
  ReportRecord report;
  if (!ReportOutboxManager::peekNext(report)) return;

  bool progressed = false;
  if (report.googleRequired && !report.googleSent) {
    if (!uploadGoogle(report.googlePayload)) { scheduleRetry(); return; }
    if (!ReportOutboxManager::markGoogleSent(report)) { scheduleRetry(); return; }
    progressed = true;
  }
  if (report.telegramRequired && !report.telegramSent) {
    bool sent = false;
    if (report.telegramKind == ReportTelegramKind::Loss) {
      sent = TelegramClient::sendLoss(report.lossCode, report.lossDurationSeconds);
    } else if (report.telegramKind == ReportTelegramKind::Document) {
      sent = sendTelegramDocument(report.csvPath);
    }
    if (!sent) { scheduleRetry(); return; }
    if (!ReportOutboxManager::markTelegramSent(report)) { scheduleRetry(); return; }
    progressed = true;
  }
  if (report.googleSent && report.telegramSent) ReportOutboxManager::removeIfComplete(report);
  if (progressed) {
    gFailureStreak = 0;
    gNextRetryMs = millis() + kSuccessCooldownMs;
  }
}
}

void CloudManager::begin() {
  HttpClientManager::begin(10000);
  TimeManager::begin();
  TelegramClient::begin();
  ReportOutboxManager::begin();
  ReportingCsvManager::begin();
  ShiftCsvManager::begin();
  loadTelegramConfig();
  gLastCompletedHour = loadNumber(kHourStatePath);
  gLastMonthKey = loadNumber(kMonthStatePath);
  gConnected = false;
  gNextRetryMs = 0;
  gFailureStreak = 0;
}

void CloudManager::update() {
  TimeManager::update();
  processEvents();
  processShiftReports();
  processHourly();
  TelegramClient::update();
  dispatchOne();
}

void CloudManager::queueStatusNow() {
  const uint32_t epoch = static_cast<uint32_t>(TimeManager::now());
  ReportOutboxManager::enqueue("status", 30, epoch, buildSnapshotPayload("status", epoch),
                               googleConfigured(), false);
}

bool CloudManager::connected() { return gConnected && WiFiManager::connected(); }
uint32_t CloudManager::uploadSuccessCount() { return gSuccess; }
uint32_t CloudManager::uploadFailureCount() { return gFailure; }
