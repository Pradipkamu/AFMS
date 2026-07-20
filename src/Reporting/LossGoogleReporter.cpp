#include "LossGoogleReporter.h"
#include "ReportOutboxManager.h"
#include "../Communication/TimeManager.h"
#include "../Core/Config.h"
#include "../Core/LossCatalog.h"
#include "../Core/Logger.h"
#include "../Machine/MachineEngine.h"
#include "../Machine/ShiftManager.h"

namespace {
uint32_t sequenceNumber = 0;

String escapeJson(const char *value) {
  String output;
  if (!value) return output;
  while (*value) {
    if (*value == '"' || *value == '\\') output += '\\';
    output += *value++;
  }
  return output;
}

String makePayload(uint16_t code, uint32_t seconds) {
  const MachineSnapshot machine = MachineEngine::snapshot();
  const ShiftSnapshot shift = ShiftManager::snapshot();
  String payload;
  payload.reserve(640);
  payload += F("{\"record_type\":\"event\",\"api_token\":\"");
  payload += escapeJson(Config::apiToken());
  payload += F("\",\"machine_id\":\""); payload += escapeJson(Config::machineId());
  payload += F("\",\"machine_name\":\""); payload += escapeJson(Config::machineName());
  payload += F("\",\"timestamp\":\""); payload += TimeManager::iso8601();
  payload += F("\",\"event_name\":\"loss_selected\",\"event_value\":"); payload += code;
  payload += F(",\"duration_seconds\":"); payload += seconds;
  payload += F(",\"loss_code\":"); payload += code;
  payload += F(",\"loss_name\":\""); payload += escapeJson(LossCatalog::name(code)); payload += '"';
  payload += F(",\"loss_duration_seconds\":"); payload += seconds;
  payload += F(",\"state\":"); payload += static_cast<uint8_t>(machine.state);
  payload += F(",\"shift\":"); payload += shift.shiftId;
  payload += F(",\"operator_id\":"); payload += shift.operatorId;
  payload += F(",\"part_number\":"); payload += shift.partNumber;
  payload += F(",\"part_name\":\""); payload += escapeJson(shift.partName);
  payload += F("\",\"total\":"); payload += machine.totalParts;
  payload += F(",\"reject\":"); payload += machine.rejectParts;
  payload += F(",\"good\":"); payload += machine.goodParts;
  payload += F(",\"alarm\":"); payload += machine.alarmActive ? F("true") : F("false");
  payload += F("}");
  return payload;
}
}

bool LossGoogleReporter::queue(uint16_t code, uint32_t seconds) {
  if (code < 1 || code > 16) return false;
  const time_t now = TimeManager::now();
  const uint32_t epoch = now > 0 ? static_cast<uint32_t>(now) : 0;

  ReportOutboxManager::ReportRecord report;
  report.type = ReportOutboxManager::ReportType::LossEvent;
  report.createdEpoch = epoch;
  report.priority = ReportOutboxManager::defaultPriority(report.type);
  report.googleRequired = true;
  report.telegramRequired = false;
  report.payload = makePayload(code, seconds);
  report.reportId = F("LOSS-DIRECT-");
  report.reportId += epoch;
  report.reportId += '-';
  report.reportId += code;
  report.reportId += '-';
  report.reportId += ++sequenceNumber;

  const bool stored = ReportOutboxManager::enqueue(report);
  if (stored) Logger::info(String(F("[LOSS] Google report persisted directly: ")) + report.reportId);
  else Logger::error(F("[LOSS] Direct Google report persistence failed"));
  return stored;
}
