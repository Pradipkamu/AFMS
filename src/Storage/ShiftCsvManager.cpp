#include "ShiftCsvManager.h"
#include "../Core/Config.h"
#include "../Core/Logger.h"
#include <LittleFS.h>
#include <time.h>

namespace {
char gMonthlyPath[40] = "";
String gDailyReportPath;

String csvText(const char *value) {
  String out("\"");
  if (value) {
    while (*value) {
      if (*value == '"') out += '"';
      out += *value++;
    }
  }
  out += '"';
  return out;
}

bool makePath(time_t epoch) {
  struct tm value;
  if (!localtime_r(&epoch, &value)) return false;
  snprintf(gMonthlyPath, sizeof(gMonthlyPath), "/reports/shift_%04d-%02d.csv",
           value.tm_year + 1900, value.tm_mon + 1);
  return true;
}

String formatEpoch(uint32_t epoch) {
  if (!epoch) return String();
  struct tm value;
  const time_t raw = static_cast<time_t>(epoch);
  if (!localtime_r(&raw, &value)) return String();
  char buffer[24];
  strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &value);
  return String(buffer);
}
}

bool ShiftCsvManager::begin() {
  if (!LittleFS.exists("/reports")) LittleFS.mkdir("/reports");
  return true;
}

bool ShiftCsvManager::appendShift(const ShiftSnapshot &shift,
                                  const OEESnapshot &oee,
                                  uint32_t endedAtEpoch) {
  if (!makePath(static_cast<time_t>(endedAtEpoch))) return false;

  const bool newFile = !LittleFS.exists(gMonthlyPath);
  File file = LittleFS.open(gMonthlyPath, "a");
  if (!file) {
    Logger::warn(F("[CSV] Unable to open monthly shift report"));
    return false;
  }

  if (newFile) {
    file.println(F("Start Time,End Time,Shift ID,Shift Name,Machine ID,Machine Name,Operator ID,Part Number,Part Name,Target,Production,Reject,Good,Availability %,Performance %,Quality %,OEE %"));
  }

  file.print(csvText(formatEpoch(shift.startedAtEpoch).c_str())); file.print(',');
  file.print(csvText(formatEpoch(endedAtEpoch).c_str())); file.print(',');
  file.print(shift.shiftId); file.print(',');
  file.print(csvText(Config::shiftName(shift.shiftId - 1))); file.print(',');
  file.print(csvText(Config::machineId())); file.print(',');
  file.print(csvText(Config::machineName())); file.print(',');
  file.print(shift.operatorId); file.print(',');
  file.print(shift.partNumber); file.print(',');
  file.print(csvText(shift.partName)); file.print(',');
  file.print(shift.targetQuantity); file.print(',');
  file.print(shift.production); file.print(',');
  file.print(shift.reject); file.print(',');
  file.print(shift.good); file.print(',');
  file.print(oee.availabilityPermille / 10.0f, 1); file.print(',');
  file.print(oee.performancePermille / 10.0f, 1); file.print(',');
  file.print(oee.qualityPermille / 10.0f, 1); file.print(',');
  file.println(oee.oeePermille / 10.0f, 1);
  file.close();

  Logger::info(String(F("[CSV] Shift appended: ")) + gMonthlyPath);

  // Send the updated CSV after every normal or recovered shift completion.
  gDailyReportPath = gMonthlyPath;
  return true;
}

const char *ShiftCsvManager::currentMonthlyPath() { return gMonthlyPath; }

bool ShiftCsvManager::consumeDailyReportReady(String &path) {
  if (!gDailyReportPath.length()) return false;
  path = gDailyReportPath;
  gDailyReportPath = "";
  return true;
}
