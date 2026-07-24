#pragma once
#include <Arduino.h>

enum class ReportTelegramKind : uint8_t {
  None = 0,
  Loss = 1,
  Document = 2
};

struct ReportRecord {
  String storagePath;
  String reportId;
  String type;
  uint16_t priority;
  uint32_t createdEpoch;
  bool googleRequired;
  bool googleSent;
  bool telegramRequired;
  bool telegramSent;
  String googlePayload;
  ReportTelegramKind telegramKind;
  uint16_t lossCode;
  uint32_t lossDurationSeconds;
  String csvPath;
};

namespace ReportOutboxManager {
bool begin();
bool enqueue(const char *type,
             uint16_t priority,
             uint32_t createdEpoch,
             const String &googlePayload,
             bool googleRequired,
             bool telegramRequired,
             ReportTelegramKind telegramKind = ReportTelegramKind::None,
             uint16_t lossCode = 0,
             uint32_t lossDurationSeconds = 0,
             const String &csvPath = String());
bool peekNext(ReportRecord &record);
bool markGoogleSent(ReportRecord &record);
bool markTelegramSent(ReportRecord &record);
bool removeIfComplete(const ReportRecord &record);
uint16_t count();
}
