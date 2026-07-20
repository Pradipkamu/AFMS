#pragma once

#include "ReportOutboxManager.h"
#include <LittleFS.h>

namespace LegacyReportingMigration {

struct Result {
  uint16_t googleRecordsImported = 0;
  uint16_t hourlyRecordsImported = 0;
  uint16_t invalidRecordsSkipped = 0;
  bool googleQueueCompleted = false;
  bool hourlyQueueCompleted = false;
};

inline uint32_t &sequence() {
  static uint32_t value = 0;
  return value;
}

inline String legacyId(const __FlashStringHelper *prefix, uint32_t epoch) {
  String id(prefix);
  id += '-';
  id += epoch;
  id += '-';
  id += ++sequence();
  return id;
}

inline bool importGooglePayload(const String &payload,
                                ReportOutboxManager::ReportType type,
                                uint32_t createdEpoch,
                                uint16_t &counter) {
  if (!payload.length()) return false;
  ReportOutboxManager::ReportRecord report;
  report.reportId = legacyId(F("LEGACY-GOOGLE"), createdEpoch);
  report.type = type;
  report.createdEpoch = createdEpoch;
  report.priority = ReportOutboxManager::defaultPriority(type);
  report.googleRequired = true;
  report.payload = payload;
  if (!ReportOutboxManager::enqueue(report)) return false;
  if (counter < 65535U) ++counter;
  return true;
}

inline bool migrateLineFile(const char *sourcePath,
                            const char *completedPath,
                            ReportOutboxManager::ReportType type,
                            bool hourlyFormat,
                            uint16_t &imported,
                            uint16_t &invalid) {
  if (!LittleFS.exists(sourcePath)) return true;
  File source = LittleFS.open(sourcePath, "r");
  if (!source) return false;

  bool success = true;
  while (source.available()) {
    String line = source.readStringUntil('\n');
    line.trim();
    if (!line.length()) continue;

    uint32_t epoch = static_cast<uint32_t>(time(nullptr));
    String payload = line;
    if (hourlyFormat) {
      const int separator = line.indexOf('\t');
      if (separator <= 0) {
        if (invalid < 65535U) ++invalid;
        continue;
      }
      epoch = static_cast<uint32_t>(strtoul(line.substring(0, separator).c_str(), nullptr, 10));
      payload = line.substring(separator + 1);
    }

    if (!payload.length() ||
        !importGooglePayload(payload, type, epoch, imported)) {
      success = false;
      break;
    }
  }
  source.close();
  if (!success) return false;

  LittleFS.remove(completedPath);
  if (!LittleFS.rename(sourcePath, completedPath)) return false;
  return true;
}

inline Result run() {
  Result result;
  result.googleQueueCompleted = migrateLineFile(
      "/offline.queue",
      "/offline.queue.migrated",
      ReportOutboxManager::ReportType::LegacyGooglePayload,
      false,
      result.googleRecordsImported,
      result.invalidRecordsSkipped);

  result.hourlyQueueCompleted = migrateLineFile(
      "/hourly.pending",
      "/hourly.pending.migrated",
      ReportOutboxManager::ReportType::HourlySummary,
      true,
      result.hourlyRecordsImported,
      result.invalidRecordsSkipped);
  return result;
}

inline uint8_t usedPercent() {
  FSInfo info;
  if (!LittleFS.info(info) || info.totalBytes == 0) return 0;
  const uint32_t percent = (info.usedBytes * 100UL) / info.totalBytes;
  return static_cast<uint8_t>(percent > 100UL ? 100UL : percent);
}

inline void cleanupMigrationBackups(uint8_t maximumUsedPercent = 85) {
  if (usedPercent() < maximumUsedPercent) return;
  // These files are removed only after every legacy record was copied into
  // independent outbox files. Unsent outbox reports are never touched here.
  LittleFS.remove("/offline.queue.migrated");
  LittleFS.remove("/hourly.pending.migrated");
  LittleFS.remove("/offline.tmp");
  LittleFS.remove("/hourly.pending.tmp");
}

}  // namespace LegacyReportingMigration
