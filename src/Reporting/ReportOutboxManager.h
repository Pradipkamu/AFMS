#pragma once

#include <Arduino.h>

namespace ReportOutboxManager {

enum class ReportType : uint8_t {
  HourlySummary = 0,
  LossEvent = 1,
  ShiftSummary = 2,
  RecoveredShiftSummary = 3,
  MonthlyDocument = 4,
  LegacyGooglePayload = 5
};

enum class Destination : uint8_t {
  Google = 0,
  Telegram = 1
};

struct ReportRecord {
  uint8_t schema = 1;
  String reportId;
  ReportType type = ReportType::HourlySummary;
  uint32_t createdEpoch = 0;
  uint8_t priority = 0;
  bool googleRequired = false;
  bool googleSent = false;
  bool telegramRequired = false;
  bool telegramSent = false;
  String documentPath;
  String payload;
  String storagePath;
};

void begin();
void update();

bool enqueue(const ReportRecord &record, String *storedPath = nullptr);
bool load(const String &path, ReportRecord &record);
bool save(const ReportRecord &record);
bool remove(const String &path);

bool nextPending(Destination destination, ReportRecord &record);
bool acknowledge(const String &path, Destination destination);
bool completed(const ReportRecord &record);
bool removeIfCompleted(const String &path);

uint16_t pendingCount();
uint16_t pendingCount(Destination destination);
uint32_t writeSuccessCount();
uint32_t writeFailureCount();

uint8_t defaultPriority(ReportType type);
const __FlashStringHelper *typeName(ReportType type);

}  // namespace ReportOutboxManager
