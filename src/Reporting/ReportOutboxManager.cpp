#include "ReportOutboxManager.h"

#include "../Core/Logger.h"
#include <ArduinoJson.h>
#include <LittleFS.h>

namespace {
constexpr const char *kOutboxDir = "/outbox";
constexpr const char *kTempPath = "/outbox/.report.tmp";
constexpr const char *kBackupPath = "/outbox/.report.bak";
constexpr uint8_t kSchema = 1;

uint16_t gSequence = 0;
uint16_t gPendingCount = 0;
uint32_t gWriteSuccess = 0;
uint32_t gWriteFailure = 0;

const char *typeToken(ReportOutboxManager::ReportType type) {
  using ReportOutboxManager::ReportType;
  switch (type) {
    case ReportType::HourlySummary: return "hourly_summary";
    case ReportType::LossEvent: return "loss_event";
    case ReportType::ShiftSummary: return "shift_summary";
    case ReportType::RecoveredShiftSummary: return "recovered_shift_summary";
    case ReportType::MonthlyDocument: return "monthly_document";
    case ReportType::LegacyGooglePayload: return "legacy_google_payload";
  }
  return "hourly_summary";
}

ReportOutboxManager::ReportType parseType(const char *value) {
  using ReportOutboxManager::ReportType;
  if (!value) return ReportType::HourlySummary;
  if (strcmp(value, "loss_event") == 0) return ReportType::LossEvent;
  if (strcmp(value, "shift_summary") == 0) return ReportType::ShiftSummary;
  if (strcmp(value, "recovered_shift_summary") == 0) return ReportType::RecoveredShiftSummary;
  if (strcmp(value, "monthly_document") == 0) return ReportType::MonthlyDocument;
  if (strcmp(value, "legacy_google_payload") == 0) return ReportType::LegacyGooglePayload;
  return ReportType::HourlySummary;
}

bool isReportPath(const String &path) {
  return path.startsWith(String(kOutboxDir) + "/R_") && path.endsWith(".json");
}

String safeId(const String &value) {
  String result;
  result.reserve(value.length());
  for (size_t index = 0; index < value.length(); ++index) {
    const char ch = value[index];
    const bool valid = (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
                       (ch >= '0' && ch <= '9') || ch == '-' || ch == '_';
    result += valid ? ch : '_';
  }
  return result;
}

String makePath(const ReportOutboxManager::ReportRecord &record) {
  String id = safeId(record.reportId);
  if (!id.length()) {
    id = String(record.createdEpoch);
    id += '_';
    id += ++gSequence;
  }
  return String(kOutboxDir) + "/R_" + id + ".json";
}

bool serializeRecord(const ReportOutboxManager::ReportRecord &record, File &file) {
  DynamicJsonDocument document(1536 + record.payload.length());
  document["schema"] = kSchema;
  document["report_id"] = record.reportId;
  document["type"] = typeToken(record.type);
  document["created_epoch"] = record.createdEpoch;
  document["priority"] = record.priority;
  document["google_required"] = record.googleRequired;
  document["google_sent"] = record.googleSent;
  document["telegram_required"] = record.telegramRequired;
  document["telegram_sent"] = record.telegramSent;
  document["document_path"] = record.documentPath;
  document["payload"] = record.payload;
  return serializeJson(document, file) > 0;
}

bool transactionalReplace(const String &finalPath, const ReportOutboxManager::ReportRecord &record) {
  LittleFS.remove(kTempPath);
  File temp = LittleFS.open(kTempPath, "w");
  if (!temp) return false;
  const bool written = serializeRecord(record, temp);
  temp.flush();
  temp.close();
  if (!written) {
    LittleFS.remove(kTempPath);
    return false;
  }

  LittleFS.remove(kBackupPath);
  const bool existed = LittleFS.exists(finalPath);
  if (existed && !LittleFS.rename(finalPath, kBackupPath)) {
    LittleFS.remove(kTempPath);
    return false;
  }

  if (!LittleFS.rename(kTempPath, finalPath)) {
    if (existed) LittleFS.rename(kBackupPath, finalPath);
    LittleFS.remove(kTempPath);
    return false;
  }

  LittleFS.remove(kBackupPath);
  return true;
}

void recoverTransactions() {
  if (LittleFS.exists(kTempPath)) LittleFS.remove(kTempPath);
  if (!LittleFS.exists(kBackupPath)) return;

  File backup = LittleFS.open(kBackupPath, "r");
  if (!backup) {
    LittleFS.remove(kBackupPath);
    return;
  }
  DynamicJsonDocument document(256);
  const DeserializationError error = deserializeJson(document, backup);
  backup.close();
  if (error) {
    LittleFS.remove(kBackupPath);
    return;
  }

  const String id = document["report_id"] | "";
  const String restoredPath = String(kOutboxDir) + "/R_" + safeId(id) + ".json";
  if (!LittleFS.exists(restoredPath)) LittleFS.rename(kBackupPath, restoredPath);
  else LittleFS.remove(kBackupPath);
}

uint16_t recount() {
  uint16_t count = 0;
  Dir directory = LittleFS.openDir(kOutboxDir);
  while (directory.next()) {
    if (isReportPath(directory.fileName()) && count < 65535U) ++count;
  }
  return count;
}
}  // namespace

namespace ReportOutboxManager {

void begin() {
  if (!LittleFS.exists(kOutboxDir)) LittleFS.mkdir(kOutboxDir);
  recoverTransactions();
  gPendingCount = recount();
  Logger::info(String(F("[OUTBOX] Restored report files: ")) + gPendingCount);
}

void update() {}

bool enqueue(const ReportRecord &input, String *storedPath) {
  ReportRecord record = input;
  record.schema = kSchema;
  if (record.priority == 0) record.priority = defaultPriority(record.type);
  const String path = makePath(record);
  record.storagePath = path;

  if (LittleFS.exists(path)) {
    ReportRecord existing;
    if (load(path, existing) && existing.reportId == record.reportId) {
      if (storedPath) *storedPath = path;
      return true;
    }
    ++gWriteFailure;
    return false;
  }

  if (!transactionalReplace(path, record)) {
    ++gWriteFailure;
    Logger::error(F("[OUTBOX] Failed to persist report"));
    return false;
  }

  if (gPendingCount < 65535U) ++gPendingCount;
  ++gWriteSuccess;
  if (storedPath) *storedPath = path;
  return true;
}

bool load(const String &path, ReportRecord &record) {
  File file = LittleFS.open(path, "r");
  if (!file) return false;
  DynamicJsonDocument document(1536 + file.size());
  const DeserializationError error = deserializeJson(document, file);
  file.close();
  if (error || document["schema"].as<uint8_t>() != kSchema) return false;

  record.schema = document["schema"] | kSchema;
  record.reportId = document["report_id"] | "";
  record.type = parseType(document["type"] | "hourly_summary");
  record.createdEpoch = document["created_epoch"] | 0UL;
  record.priority = document["priority"] | defaultPriority(record.type);
  record.googleRequired = document["google_required"] | false;
  record.googleSent = document["google_sent"] | false;
  record.telegramRequired = document["telegram_required"] | false;
  record.telegramSent = document["telegram_sent"] | false;
  record.documentPath = document["document_path"] | "";
  record.payload = document["payload"] | "";
  record.storagePath = path;
  return record.reportId.length() != 0;
}

bool save(const ReportRecord &record) {
  if (!record.storagePath.length() || !isReportPath(record.storagePath)) return false;
  if (!transactionalReplace(record.storagePath, record)) {
    ++gWriteFailure;
    return false;
  }
  ++gWriteSuccess;
  return true;
}

bool remove(const String &path) {
  if (!isReportPath(path) || !LittleFS.remove(path)) return false;
  if (gPendingCount > 0) --gPendingCount;
  return true;
}

bool nextPending(Destination destination, ReportRecord &selected) {
  bool found = false;
  Dir directory = LittleFS.openDir(kOutboxDir);
  while (directory.next()) {
    const String path = directory.fileName();
    if (!isReportPath(path)) continue;
    ReportRecord candidate;
    if (!load(path, candidate)) continue;

    const bool pending = destination == Destination::Google
                             ? candidate.googleRequired && !candidate.googleSent
                             : candidate.telegramRequired && !candidate.telegramSent;
    if (!pending) continue;

    if (!found || candidate.priority > selected.priority ||
        (candidate.priority == selected.priority && candidate.createdEpoch < selected.createdEpoch)) {
      selected = candidate;
      found = true;
    }
  }
  return found;
}

bool acknowledge(const String &path, Destination destination) {
  ReportRecord record;
  if (!load(path, record)) return false;
  if (destination == Destination::Google) record.googleSent = true;
  else record.telegramSent = true;
  return save(record);
}

bool completed(const ReportRecord &record) {
  return (!record.googleRequired || record.googleSent) &&
         (!record.telegramRequired || record.telegramSent);
}

bool removeIfCompleted(const String &path) {
  ReportRecord record;
  return load(path, record) && completed(record) && remove(path);
}

uint16_t pendingCount() { return gPendingCount; }

uint16_t pendingCount(Destination destination) {
  uint16_t count = 0;
  ReportRecord record;
  Dir directory = LittleFS.openDir(kOutboxDir);
  while (directory.next()) {
    if (!load(directory.fileName(), record)) continue;
    const bool pending = destination == Destination::Google
                             ? record.googleRequired && !record.googleSent
                             : record.telegramRequired && !record.telegramSent;
    if (pending && count < 65535U) ++count;
  }
  return count;
}

uint32_t writeSuccessCount() { return gWriteSuccess; }
uint32_t writeFailureCount() { return gWriteFailure; }

uint8_t defaultPriority(ReportType type) {
  switch (type) {
    case ReportType::RecoveredShiftSummary: return 120;
    case ReportType::ShiftSummary: return 110;
    case ReportType::MonthlyDocument: return 100;
    case ReportType::LossEvent: return 80;
    case ReportType::HourlySummary: return 40;
    case ReportType::LegacyGooglePayload: return 30;
  }
  return 40;
}

const __FlashStringHelper *typeName(ReportType type) {
  switch (type) {
    case ReportType::HourlySummary: return F("hourly_summary");
    case ReportType::LossEvent: return F("loss_event");
    case ReportType::ShiftSummary: return F("shift_summary");
    case ReportType::RecoveredShiftSummary: return F("recovered_shift_summary");
    case ReportType::MonthlyDocument: return F("monthly_document");
    case ReportType::LegacyGooglePayload: return F("legacy_google_payload");
  }
  return F("unknown");
}

}  // namespace ReportOutboxManager
