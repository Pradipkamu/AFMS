#include "ReportOutboxManager.h"
#include "../Core/Config.h"
#include "../Core/Logger.h"
#include <ArduinoJson.h>
#include <LittleFS.h>

namespace {
constexpr const char *kDir = "/outbox";
constexpr const char *kSequencePath = "/outbox.seq";
uint32_t gSequence = 0;
uint16_t gCount = 0;

bool readRecord(const String &path, ReportRecord &record) {
  File file = LittleFS.open(path, "r");
  if (!file) return false;
  DynamicJsonDocument document(4096);
  const DeserializationError error = deserializeJson(document, file);
  file.close();
  if (error || (document["version"] | 0U) != 1U) return false;

  record.storagePath = path;
  record.reportId = document["report_id"] | "";
  record.type = document["type"] | "";
  record.priority = document["priority"] | 0U;
  record.createdEpoch = document["created_epoch"] | 0UL;
  record.googleRequired = document["google_required"] | false;
  record.googleSent = document["google_sent"] | false;
  record.telegramRequired = document["telegram_required"] | false;
  record.telegramSent = document["telegram_sent"] | false;
  record.googlePayload = document["google_payload"] | "";
  record.telegramKind = static_cast<ReportTelegramKind>(document["telegram_kind"] | 0U);
  record.lossCode = document["loss_code"] | 0U;
  record.lossDurationSeconds = document["loss_duration_seconds"] | 0UL;
  record.csvPath = document["csv_path"] | "";
  return record.reportId.length() && record.type.length();
}

bool writeRecord(const ReportRecord &record) {
  DynamicJsonDocument document(4096);
  document["version"] = 1;
  document["report_id"] = record.reportId;
  document["type"] = record.type;
  document["priority"] = record.priority;
  document["created_epoch"] = record.createdEpoch;
  document["google_required"] = record.googleRequired;
  document["google_sent"] = record.googleSent;
  document["telegram_required"] = record.telegramRequired;
  document["telegram_sent"] = record.telegramSent;
  document["google_payload"] = record.googlePayload;
  document["telegram_kind"] = static_cast<uint8_t>(record.telegramKind);
  document["loss_code"] = record.lossCode;
  document["loss_duration_seconds"] = record.lossDurationSeconds;
  document["csv_path"] = record.csvPath;

  const String tempPath = record.storagePath + F(".tmp");
  File file = LittleFS.open(tempPath, "w");
  if (!file) return false;
  const size_t written = serializeJson(document, file);
  file.flush();
  file.close();
  if (!written) {
    LittleFS.remove(tempPath);
    return false;
  }
  LittleFS.remove(record.storagePath);
  if (!LittleFS.rename(tempPath, record.storagePath)) {
    LittleFS.remove(tempPath);
    return false;
  }
  return true;
}

void loadSequence() {
  File file = LittleFS.open(kSequencePath, "r");
  if (!file) return;
  gSequence = static_cast<uint32_t>(file.parseInt());
  file.close();
}

bool saveSequence() {
  File file = LittleFS.open(kSequencePath, "w");
  if (!file) return false;
  file.print(gSequence);
  file.flush();
  file.close();
  return true;
}

uint16_t recount() {
  uint16_t count = 0;
  Dir dir = LittleFS.openDir(kDir);
  while (dir.next()) {
    if (dir.fileName().endsWith(F(".json")) && count < 65535U) ++count;
  }
  return count;
}

String nextPath() {
  ++gSequence;
  saveSequence();
  char path[36];
  snprintf(path, sizeof(path), "/outbox/r%010lu.json", static_cast<unsigned long>(gSequence));
  return String(path);
}

String makeReportId(const char *type, uint32_t epoch) {
  String value = Config::machineId();
  value += '-';
  value += type ? type : "report";
  value += '-';
  value += epoch;
  value += '-';
  value += gSequence;
  return value;
}
}

bool ReportOutboxManager::begin() {
  if (!LittleFS.exists(kDir) && !LittleFS.mkdir(kDir)) return false;
  loadSequence();
  gCount = recount();
  Logger::info(String(F("[OUTBOX] Restored records: ")) + gCount);
  return true;
}

bool ReportOutboxManager::enqueue(const char *type,
                                  uint16_t priority,
                                  uint32_t createdEpoch,
                                  const String &googlePayload,
                                  bool googleRequired,
                                  bool telegramRequired,
                                  ReportTelegramKind telegramKind,
                                  uint16_t lossCode,
                                  uint32_t lossDurationSeconds,
                                  const String &csvPath) {
  ReportRecord record;
  record.storagePath = nextPath();
  record.reportId = makeReportId(type, createdEpoch);
  record.type = type ? type : "report";
  record.priority = priority;
  record.createdEpoch = createdEpoch;
  record.googleRequired = googleRequired;
  record.googleSent = !googleRequired;
  record.telegramRequired = telegramRequired;
  record.telegramSent = !telegramRequired;
  record.googlePayload = googlePayload;
  record.telegramKind = telegramKind;
  record.lossCode = lossCode;
  record.lossDurationSeconds = lossDurationSeconds;
  record.csvPath = csvPath;
  if (!writeRecord(record)) {
    Logger::error(F("[OUTBOX] Failed to persist report"));
    return false;
  }
  if (gCount < 65535U) ++gCount;
  Logger::info(String(F("[OUTBOX] Queued ")) + record.reportId);
  return true;
}

bool ReportOutboxManager::peekNext(ReportRecord &best) {
  bool found = false;
  Dir dir = LittleFS.openDir(kDir);
  while (dir.next()) {
    const String path = dir.fileName();
    if (!path.endsWith(F(".json"))) continue;
    ReportRecord candidate;
    if (!readRecord(path, candidate)) continue;
    if (candidate.googleSent && candidate.telegramSent) {
      LittleFS.remove(path);
      if (gCount) --gCount;
      continue;
    }
    if (!found || candidate.priority > best.priority ||
        (candidate.priority == best.priority && candidate.createdEpoch < best.createdEpoch)) {
      best = candidate;
      found = true;
    }
  }
  return found;
}

bool ReportOutboxManager::markGoogleSent(ReportRecord &record) {
  record.googleSent = true;
  return writeRecord(record);
}

bool ReportOutboxManager::markTelegramSent(ReportRecord &record) {
  record.telegramSent = true;
  return writeRecord(record);
}

bool ReportOutboxManager::removeIfComplete(const ReportRecord &record) {
  if (!record.googleSent || !record.telegramSent) return false;
  if (!LittleFS.remove(record.storagePath)) return false;
  if (gCount) --gCount;
  return true;
}

uint16_t ReportOutboxManager::count() { return gCount; }
