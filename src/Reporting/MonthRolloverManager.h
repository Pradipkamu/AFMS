#pragma once

#include "ReportOutboxManager.h"
#include <ArduinoJson.h>
#include <LittleFS.h>
#include <time.h>

namespace MonthRolloverManager {

struct State {
  uint16_t activeYear = 0;
  uint8_t activeMonth = 0;
  uint16_t closingYear = 0;
  uint8_t closingMonth = 0;
  String documentPath;
  String outboxPath;
  bool closePending = false;
};

inline State &state() {
  static State value;
  return value;
}

inline String twoDigits(uint8_t value) {
  String result;
  if (value < 10) result += '0';
  result += value;
  return result;
}

inline String monthlyCsvPath(uint16_t year, uint8_t month) {
  return String(F("/reports/shift_")) + year + '-' + twoDigits(month) + F(".csv");
}

inline bool saveState() {
  constexpr const char *kPath = "/month_close.json";
  constexpr const char *kTemp = "/month_close.tmp";
  File file = LittleFS.open(kTemp, "w");
  if (!file) return false;
  DynamicJsonDocument document(512);
  const State &value = state();
  document["active_year"] = value.activeYear;
  document["active_month"] = value.activeMonth;
  document["closing_year"] = value.closingYear;
  document["closing_month"] = value.closingMonth;
  document["document_path"] = value.documentPath;
  document["outbox_path"] = value.outboxPath;
  document["close_pending"] = value.closePending;
  const bool written = serializeJson(document, file) > 0;
  file.flush();
  file.close();
  if (!written) {
    LittleFS.remove(kTemp);
    return false;
  }
  LittleFS.remove(kPath);
  return LittleFS.rename(kTemp, kPath);
}

inline void loadState() {
  File file = LittleFS.open("/month_close.json", "r");
  if (!file) return;
  DynamicJsonDocument document(512);
  const DeserializationError error = deserializeJson(document, file);
  file.close();
  if (error) return;
  State &value = state();
  value.activeYear = document["active_year"] | 0;
  value.activeMonth = document["active_month"] | 0;
  value.closingYear = document["closing_year"] | 0;
  value.closingMonth = document["closing_month"] | 0;
  value.documentPath = document["document_path"] | "";
  value.outboxPath = document["outbox_path"] | "";
  value.closePending = document["close_pending"] | false;
}

inline void begin() { loadState(); }

inline bool queueClosingMonth() {
  State &value = state();
  if (!value.closePending || !value.documentPath.length()) return false;
  if (!LittleFS.exists(value.documentPath)) {
    value.closePending = false;
    value.outboxPath = String();
    saveState();
    return false;
  }

  if (value.outboxPath.length() && LittleFS.exists(value.outboxPath)) return true;

  ReportOutboxManager::ReportRecord report;
  report.reportId = String(F("MONTH-")) + value.closingYear + '-' +
                    twoDigits(value.closingMonth) + F("-SHIFT-CSV");
  report.type = ReportOutboxManager::ReportType::MonthlyDocument;
  report.createdEpoch = static_cast<uint32_t>(time(nullptr));
  report.priority = ReportOutboxManager::defaultPriority(report.type);
  report.telegramRequired = true;
  report.documentPath = value.documentPath;
  report.payload = String(F("AFMS monthly shift report: ")) + value.closingYear + '-' +
                   twoDigits(value.closingMonth);
  if (!ReportOutboxManager::enqueue(report, &value.outboxPath)) return false;
  return saveState();
}

inline void finishAcknowledgedClose() {
  State &value = state();
  if (!value.closePending || !value.outboxPath.length()) return;
  if (LittleFS.exists(value.outboxPath)) return;

  // The outbox removes monthly document records only after Telegram acknowledgement.
  if (value.documentPath.length()) LittleFS.remove(value.documentPath);
  value.closingYear = 0;
  value.closingMonth = 0;
  value.documentPath = String();
  value.outboxPath = String();
  value.closePending = false;
  saveState();
}

inline void update(bool timeSynchronized, time_t now) {
  if (!timeSynchronized || now <= 0) return;
  struct tm localTime;
  if (!localtime_r(&now, &localTime)) return;
  const uint16_t year = static_cast<uint16_t>(localTime.tm_year + 1900);
  const uint8_t month = static_cast<uint8_t>(localTime.tm_mon + 1);
  State &value = state();

  if (value.activeYear == 0 || value.activeMonth == 0) {
    value.activeYear = year;
    value.activeMonth = month;
    saveState();
  } else if ((value.activeYear != year || value.activeMonth != month) &&
             !value.closePending) {
    value.closingYear = value.activeYear;
    value.closingMonth = value.activeMonth;
    value.documentPath = monthlyCsvPath(value.closingYear, value.closingMonth);
    value.outboxPath = String();
    value.closePending = true;
    value.activeYear = year;
    value.activeMonth = month;
    saveState();
  }

  queueClosingMonth();
  finishAcknowledgedClose();
}

inline bool closePending() { return state().closePending; }

}  // namespace MonthRolloverManager
