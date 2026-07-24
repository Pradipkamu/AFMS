#include "ReportingCsvManager.h"
#include "../Core/Config.h"
#include <LittleFS.h>
#include <time.h>

namespace {
String monthPath(const char *prefix, uint32_t epoch) {
  time_t raw = static_cast<time_t>(epoch);
  struct tm value;
  if (!localtime_r(&raw, &value)) return String();
  char path[48];
  snprintf(path, sizeof(path), "/reports/%s_%04d-%02d.csv", prefix,
           value.tm_year + 1900, value.tm_mon + 1);
  return String(path);
}

String formatEpoch(uint32_t epoch) {
  time_t raw = static_cast<time_t>(epoch);
  struct tm value;
  if (!epoch || !localtime_r(&raw, &value)) return String();
  char text[24];
  strftime(text, sizeof(text), "%Y-%m-%d %H:%M:%S", &value);
  return String(text);
}

String quote(const char *value) {
  String out('"');
  if (value) while (*value) {
    if (*value == '"') out += '"';
    out += *value++;
  }
  out += '"';
  return out;
}

bool openAppend(const String &path, const __FlashStringHelper *header, File &file) {
  if (!path.length()) return false;
  const bool fresh = !LittleFS.exists(path);
  file = LittleFS.open(path, "a");
  if (!file) return false;
  if (fresh) file.println(header);
  return true;
}
}

bool ReportingCsvManager::begin() {
  if (!LittleFS.exists("/reports")) return LittleFS.mkdir("/reports");
  return true;
}

String ReportingCsvManager::lossPath(uint32_t epoch) { return monthPath("loss", epoch); }
String ReportingCsvManager::hourlyPath(uint32_t epoch) { return monthPath("hourly", epoch); }
String ReportingCsvManager::statisticsPath(uint32_t epoch) { return monthPath("statistics", epoch); }

bool ReportingCsvManager::appendLoss(uint32_t epoch, uint16_t lossCode, const char *lossName,
                                     uint32_t durationSeconds, const MachineSnapshot &machine,
                                     const ShiftSnapshot &shift) {
  File file;
  if (!openAppend(lossPath(epoch), F("Time,Machine ID,Machine Name,Shift,Operator ID,Part Number,Part Name,Loss Code,Loss Name,Duration Seconds,Production,Reject,Good"), file)) return false;
  file.print(quote(formatEpoch(epoch).c_str())); file.print(',');
  file.print(quote(Config::machineId())); file.print(',');
  file.print(quote(Config::machineName())); file.print(',');
  file.print(shift.shiftId); file.print(','); file.print(shift.operatorId); file.print(',');
  file.print(shift.partNumber); file.print(','); file.print(quote(shift.partName)); file.print(',');
  file.print(lossCode); file.print(','); file.print(quote(lossName)); file.print(',');
  file.print(durationSeconds); file.print(','); file.print(machine.totalParts); file.print(',');
  file.print(machine.rejectParts); file.print(','); file.println(machine.goodParts);
  file.close();
  return true;
}

bool ReportingCsvManager::appendHourly(uint32_t epoch, const MachineSnapshot &machine,
                                       const ShiftSnapshot &shift, const OEESnapshot &oee,
                                       bool recovered) {
  File file;
  if (!openAppend(hourlyPath(epoch), F("Period End,Recovered,Machine ID,Machine Name,Shift,Operator ID,Part Number,Part Name,Target,Production,Reject,Good,Run Seconds,Downtime Seconds,Availability %,Performance %,Quality %,OEE %"), file)) return false;
  file.print(quote(formatEpoch(epoch).c_str())); file.print(','); file.print(recovered ? 1 : 0); file.print(',');
  file.print(quote(Config::machineId())); file.print(','); file.print(quote(Config::machineName())); file.print(',');
  file.print(shift.shiftId); file.print(','); file.print(shift.operatorId); file.print(',');
  file.print(shift.partNumber); file.print(','); file.print(quote(shift.partName)); file.print(',');
  file.print(shift.targetQuantity); file.print(','); file.print(machine.totalParts); file.print(',');
  file.print(machine.rejectParts); file.print(','); file.print(machine.goodParts); file.print(',');
  file.print(machine.runSeconds); file.print(','); file.print(machine.downtimeSeconds); file.print(',');
  file.print(oee.availabilityPermille / 10.0f, 1); file.print(',');
  file.print(oee.performancePermille / 10.0f, 1); file.print(',');
  file.print(oee.qualityPermille / 10.0f, 1); file.print(',');
  file.println(oee.oeePermille / 10.0f, 1);
  file.close();
  return true;
}

bool ReportingCsvManager::writeStatistics(uint32_t epoch, const MachineSnapshot &machine,
                                          const ShiftSnapshot &shift, const OEESnapshot &oee) {
  File file;
  if (!openAppend(statisticsPath(epoch), F("Time,Machine ID,Machine Name,Shift,Target,Production,Reject,Good,Run Seconds,Downtime Seconds,Availability %,Performance %,Quality %,OEE %"), file)) return false;
  file.print(quote(formatEpoch(epoch).c_str())); file.print(',');
  file.print(quote(Config::machineId())); file.print(','); file.print(quote(Config::machineName())); file.print(',');
  file.print(shift.shiftId); file.print(','); file.print(shift.targetQuantity); file.print(',');
  file.print(machine.totalParts); file.print(','); file.print(machine.rejectParts); file.print(',');
  file.print(machine.goodParts); file.print(','); file.print(machine.runSeconds); file.print(',');
  file.print(machine.downtimeSeconds); file.print(',');
  file.print(oee.availabilityPermille / 10.0f, 1); file.print(',');
  file.print(oee.performancePermille / 10.0f, 1); file.print(',');
  file.print(oee.qualityPermille / 10.0f, 1); file.print(',');
  file.println(oee.oeePermille / 10.0f, 1);
  file.close();
  return true;
}
