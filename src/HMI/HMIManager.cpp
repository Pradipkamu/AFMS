#include "HMIManager.h"
#include "RegisterMap.h"
#include "../Communication/RS485Driver.h"
#include "../Communication/ModbusSlave.h"
#include "../Communication/WiFiManager.h"
#include "../Communication/CloudManager.h"
#include "../Communication/TelegramClient.h"
#include "../Communication/TimeManager.h"
#include "../Storage/OfflineQueue.h"
#include "../Storage/RuntimeStateManager.h"
#include "../Machine/MachineEngine.h"
#include "../Machine/CycleManager.h"
#include "../Machine/OEEManager.h"
#include "../Machine/ShiftManager.h"
#include "../Core/Config.h"
#include "../Core/HardwareConfig.h"
#include "../Core/Logger.h"
#include <ESP8266WiFi.h>
#include <cstring>
#include <time.h>

namespace {
uint16_t gRegisters[HMIRegister::RegisterCount] = {0};
uint16_t gLastCycleSeconds = 0, gLastTargetQuantity = 0, gLastShift = 0, gLastOperatorId = 0;
uint16_t gLastHmiHeartbeat = 0, gHeartbeat = 0;
uint32_t gLastHmiHeartbeatMs = 0, gLastPartNumber = 0, gLastSyncMs = 0;
char gLastPartName[17] = "";
bool gLastAlarmActive = false;

void write32(uint16_t lowRegister, uint32_t value) {
  gRegisters[lowRegister] = static_cast<uint16_t>(value & 0xFFFFU);
  gRegisters[lowRegister + 1] = static_cast<uint16_t>(value >> 16U);
}

uint32_t read32(uint16_t lowRegister) {
  return static_cast<uint32_t>(gRegisters[lowRegister]) |
         (static_cast<uint32_t>(gRegisters[lowRegister + 1]) << 16U);
}

void writePartNameCommand(const char *source) {
  const char *text = source ? source : "";
  for (uint16_t i = 0; i < HMIRegister::CommandPartNameRegisters; ++i) {
    const size_t pos = static_cast<size_t>(i) * 2U;
    const uint8_t high = text[pos] ? static_cast<uint8_t>(text[pos]) : 0;
    const uint8_t low = (high && text[pos + 1]) ? static_cast<uint8_t>(text[pos + 1]) : 0;
    gRegisters[HMIRegister::CommandPartNameStart + i] =
        static_cast<uint16_t>((static_cast<uint16_t>(high) << 8U) | low);
  }
}

void readPartName(char *destination, size_t size) {
  if (!destination || size == 0) return;
  size_t position = 0;
  for (uint16_t i = 0; i < HMIRegister::CommandPartNameRegisters && position + 1 < size; ++i) {
    const uint16_t word = gRegisters[HMIRegister::CommandPartNameStart + i];
    const char high = static_cast<char>((word >> 8U) & 0xFFU);
    const char low = static_cast<char>(word & 0xFFU);
    if (high == '\0') break;
    destination[position++] = high;
    if (low == '\0' || position + 1 >= size) break;
    destination[position++] = low;
  }
  destination[position] = '\0';
}

uint16_t clamp16(uint32_t value) {
  return value > 0xFFFFUL ? 0xFFFFU : static_cast<uint16_t>(value);
}

uint32_t shiftDurationSeconds(uint16_t shiftId) {
  if (!Config::shiftScheduleValid() || shiftId < 1 || shiftId > 3) return 0;
  const uint16_t start = Config::shiftStartMinutes(shiftId - 1);
  const uint16_t end = Config::shiftEndMinutes(shiftId - 1);
  const uint16_t durationMinutes = end > start ? end - start : static_cast<uint16_t>(1440U - start + end);
  return static_cast<uint32_t>(durationMinutes) * 60UL;
}

uint32_t adjustedTarget(uint32_t originalTarget,
                         uint16_t shiftId,
                         uint32_t plannedShutdownSeconds) {
  const uint32_t shiftSeconds = shiftDurationSeconds(shiftId);
  if (originalTarget == 0 || shiftSeconds == 0) return originalTarget;
  const uint32_t productiveSeconds = plannedShutdownSeconds < shiftSeconds
      ? shiftSeconds - plannedShutdownSeconds
      : 0;
  return static_cast<uint32_t>((static_cast<uint64_t>(originalTarget) * productiveSeconds) /
                               shiftSeconds);
}

uint16_t consumeLossCommand() {
  uint16_t selectedCode = 0;
  uint8_t assertedCount = 0;
  for (uint16_t coil = 0; coil < 16; ++coil) {
    if (ModbusSlave::consumeCoil(coil)) {
      ++assertedCount;
      if (selectedCode == 0) selectedCode = static_cast<uint16_t>(coil + 1U);
    }
  }
  if (assertedCount > 1) {
    gRegisters[HMIRegister::StatusLossCommandResult] = 4;
    return 0;
  }
  if (selectedCode == 0) selectedCode = gRegisters[HMIRegister::CommandLossCode];
  if (selectedCode == 0) selectedCode = gRegisters[HMIRegister::CommandLossCodeAlias];
  gRegisters[HMIRegister::CommandLossCode] = 0;
  gRegisters[HMIRegister::CommandLossCodeAlias] = 0;
  return selectedCode;
}
}

void HMIManager::begin() {
  RS485Driver::begin(HardwareConfig::Rs485DirectionPin, HardwareConfig::Rs485Baud);
  ModbusSlave::begin(HardwareConfig::ModbusSlaveId, gRegisters, HMIRegister::RegisterCount);

  const ShiftSnapshot shift = ShiftManager::snapshot();
  const uint16_t cycleSeconds = static_cast<uint16_t>(CycleManager::cycleTimeMs() / 1000UL);
  gRegisters[HMIRegister::CommandCycleTimeSeconds] = cycleSeconds;
  gRegisters[HMIRegister::CommandTargetQuantity] = clamp16(shift.targetQuantity);
  gRegisters[HMIRegister::CommandOperatorId] = clamp16(shift.operatorId);
  gRegisters[HMIRegister::CommandShift] = shift.shiftId;
  write32(HMIRegister::CommandPartNumberLow, shift.partNumber);
  writePartNameCommand(shift.partName);

  gLastCycleSeconds = cycleSeconds;
  gLastTargetQuantity = gRegisters[HMIRegister::CommandTargetQuantity];
  gLastOperatorId = gRegisters[HMIRegister::CommandOperatorId];
  gLastShift = shift.shiftId;
  gLastPartNumber = shift.partNumber;
  strlcpy(gLastPartName, shift.partName, sizeof(gLastPartName));
  gLastAlarmActive = MachineEngine::snapshot().alarmActive;
  gLastHmiHeartbeatMs = millis();
  Logger::info(F("Delta HMI Modbus RTU hardware UART ready"));
  Logger::info(F("[LOSS] HMI coils 00001-00016 mapped to Loss 1-16"));
}

void HMIManager::update() {
  ModbusSlave::update();
  bool criticalChange = false;

  const uint16_t lossCode = consumeLossCommand();
  if (lossCode != 0) {
    const bool accepted = MachineEngine::acknowledgeLossCode(lossCode);
    gRegisters[HMIRegister::StatusLossCommandResult] = accepted ? 1 : 2;
    criticalChange |= accepted;
    Logger::info(String(F("[LOSS] HMI command ")) + lossCode +
                 (accepted ? F(" accepted") : F(" rejected")));
  }

  const uint16_t cycleSeconds = gRegisters[HMIRegister::CommandCycleTimeSeconds];
  if (cycleSeconds >= 1 && cycleSeconds <= 3600 && cycleSeconds != gLastCycleSeconds) {
    const uint32_t cycleMs = static_cast<uint32_t>(cycleSeconds) * 1000UL;
    CycleManager::setCycleTimeMs(cycleMs);
    OEEManager::setIdealCycleTimeMs(cycleMs);
    gLastCycleSeconds = cycleSeconds;
    criticalChange = true;
  }

  const uint16_t targetQuantity = gRegisters[HMIRegister::CommandTargetQuantity];
  if (targetQuantity != gLastTargetQuantity) {
    ShiftManager::setTargetQuantity(targetQuantity);
    gLastTargetQuantity = targetQuantity;
    criticalChange = true;
  }

  const uint16_t shift = gRegisters[HMIRegister::CommandShift];
  if (shift >= 1 && shift <= 3 && shift != gLastShift) {
    ShiftManager::setShift(shift);
    gLastShift = shift;
    criticalChange = true;
  }

  const uint16_t operatorId = gRegisters[HMIRegister::CommandOperatorId];
  if (operatorId != gLastOperatorId) {
    ShiftManager::setOperatorId(operatorId);
    gLastOperatorId = operatorId;
    criticalChange = true;
  }

  const uint16_t hmiHeartbeat = gRegisters[HMIRegister::CommandHeartbeat];
  if (hmiHeartbeat != gLastHmiHeartbeat) {
    gLastHmiHeartbeat = hmiHeartbeat;
    gLastHmiHeartbeatMs = millis();
  }

  char partName[17];
  readPartName(partName, sizeof(partName));
  const uint32_t partNumber = read32(HMIRegister::CommandPartNumberLow);
  if (partNumber != gLastPartNumber || strncmp(partName, gLastPartName, sizeof(gLastPartName)) != 0) {
    ShiftManager::setPart(partNumber, partName);
    gLastPartNumber = partNumber;
    strncpy(gLastPartName, partName, sizeof(gLastPartName) - 1);
    gLastPartName[sizeof(gLastPartName) - 1] = '\0';
    criticalChange = true;
  }
  if (criticalChange) RuntimeStateManager::scheduleSave();

  if (millis() - gLastSyncMs < 250UL) return;
  gLastSyncMs = millis();
  const MachineSnapshot machine = MachineEngine::snapshot();
  const ShiftSnapshot shiftData = ShiftManager::snapshot();
  const OEESnapshot oee = OEEManager::snapshot();

  if (machine.alarmActive && !gLastAlarmActive) {
    gRegisters[HMIRegister::StatusLossCommandResult] = 0;
  }
  gLastAlarmActive = machine.alarmActive;

  gRegisters[HMIRegister::StatusMachineState] = static_cast<uint16_t>(machine.state);
  write32(HMIRegister::StatusProductionLow, machine.totalParts);
  write32(HMIRegister::StatusRejectLow, machine.rejectParts);
  write32(HMIRegister::StatusGoodLow, machine.goodParts);
  write32(HMIRegister::StatusIdleSecondsLow, machine.idleSeconds);
  gRegisters[HMIRegister::StatusAlarm] = machine.alarmActive ? 1 : 0;
  gRegisters[HMIRegister::StatusCommunication] = ModbusSlave::connected() ? 1 : 0;
  gRegisters[HMIRegister::StatusEspHeartbeat] = ++gHeartbeat;
  write32(HMIRegister::StatusRunSecondsLow, machine.runSeconds);
  write32(HMIRegister::StatusDowntimeSecondsLow, machine.downtimeSeconds);
  gRegisters[HMIRegister::StatusAvailabilityPermille] = machine.availabilityPermille;
  gRegisters[HMIRegister::StatusPerformancePermille] = machine.performancePermille;
  gRegisters[HMIRegister::StatusQualityPermille] = machine.qualityPermille;
  gRegisters[HMIRegister::StatusOeePermille] = machine.oeePermille;
  write32(HMIRegister::StatusTargetQuantityLow, shiftData.targetQuantity);
  gRegisters[HMIRegister::StatusShift] = shiftData.shiftId;
  write32(HMIRegister::StatusOperatorIdLow, shiftData.operatorId);
  write32(HMIRegister::StatusPartNumberLow, shiftData.partNumber);
  write32(HMIRegister::StatusShiftProductionLow, shiftData.production);
  write32(HMIRegister::StatusShiftRejectLow, shiftData.reject);
  write32(HMIRegister::StatusShiftGoodLow, shiftData.good);
  write32(HMIRegister::StatusTargetRemainingLow,
          shiftData.targetQuantity > shiftData.production ? shiftData.targetQuantity - shiftData.production : 0);

  gRegisters[HMIRegister::StatusWifiConnected] = WiFiManager::connected() ? 1 : 0;
  gRegisters[HMIRegister::StatusGoogleConnected] = CloudManager::uploadSuccessCount() > 0 ? 1 : 0;
  gRegisters[HMIRegister::StatusTelegramConnected] = TelegramClient::connected() ? 1 : 0;
  gRegisters[HMIRegister::StatusOfflineQueueCount] = OfflineQueue::count();
  write32(HMIRegister::StatusGoogleSuccessLow, CloudManager::uploadSuccessCount());
  write32(HMIRegister::StatusGoogleFailureLow, CloudManager::uploadFailureCount());
  write32(HMIRegister::StatusTelegramSuccessLow, TelegramClient::successCount());
  write32(HMIRegister::StatusTelegramFailureLow, TelegramClient::failureCount());
  gRegisters[HMIRegister::StatusModbusErrorCount] = static_cast<uint16_t>(ModbusSlave::errorCount() & 0xFFFFU);
  write32(HMIRegister::StatusModbusRequestLow, ModbusSlave::requestCount());
  gRegisters[HMIRegister::StatusWifiRssi] = WiFiManager::connected()
      ? static_cast<uint16_t>(static_cast<int16_t>(WiFi.RSSI()))
      : 0;
  gRegisters[HMIRegister::StatusTimeSynchronized] = TimeManager::synchronized() ? 1 : 0;

  const time_t now = TimeManager::now();
  struct tm localTime;
  if (now > 0 && localtime_r(&now, &localTime)) {
    gRegisters[HMIRegister::StatusYear] = static_cast<uint16_t>(localTime.tm_year + 1900);
    gRegisters[HMIRegister::StatusMonth] = static_cast<uint16_t>(localTime.tm_mon + 1);
    gRegisters[HMIRegister::StatusDay] = static_cast<uint16_t>(localTime.tm_mday);
    gRegisters[HMIRegister::StatusHour] = static_cast<uint16_t>(localTime.tm_hour);
    gRegisters[HMIRegister::StatusMinute] = static_cast<uint16_t>(localTime.tm_min);
    gRegisters[HMIRegister::StatusSecond] = static_cast<uint16_t>(localTime.tm_sec);
  }

  gRegisters[HMIRegister::StatusCycleTimeSeconds] = static_cast<uint16_t>(CycleManager::cycleTimeMs() / 1000UL);
  gRegisters[HMIRegister::StatusHmiHeartbeatEcho] = gLastHmiHeartbeat;
  gRegisters[HMIRegister::StatusHmiHeartbeatAgeSeconds] = clamp16((millis() - gLastHmiHeartbeatMs) / 1000UL);
  const uint32_t lastRequestMs = ModbusSlave::lastRequestMs();
  gRegisters[HMIRegister::StatusLastModbusAgeMs] = lastRequestMs == 0
      ? 0xFFFFU
      : clamp16(millis() - lastRequestMs);
  write32(HMIRegister::StatusScheduledShiftElapsedLow, oee.scheduledShiftElapsedSeconds);
  write32(HMIRegister::StatusPlannedShutdownLow, oee.plannedShutdownSeconds);
  write32(HMIRegister::StatusPlannedProductionLow, oee.plannedSeconds);
  gRegisters[HMIRegister::StatusLastLossCode] = MachineEngine::lastAcceptedLossCode();
  write32(HMIRegister::StatusLastLossDurationLow, MachineEngine::lastLossDurationSeconds());

  const uint32_t reducedTarget = adjustedTarget(shiftData.targetQuantity,
                                                 shiftData.shiftId,
                                                 oee.plannedShutdownSeconds);
  const uint32_t reducedRemaining = reducedTarget > shiftData.production
      ? reducedTarget - shiftData.production
      : 0;
  write32(HMIRegister::StatusAdjustedTargetLow, reducedTarget);
  write32(HMIRegister::StatusAdjustedTargetRemainingLow, reducedRemaining);
}

bool HMIManager::connected() { return ModbusSlave::connected(); }
uint32_t HMIManager::requestCount() { return ModbusSlave::requestCount(); }
uint32_t HMIManager::errorCount() { return ModbusSlave::errorCount(); }
