#include "HMIManager.h"
#include "RegisterMap.h"
#include "../Communication/RS485Driver.h"
#include "../Communication/ModbusSlave.h"
#include "../Machine/MachineEngine.h"
#include "../Machine/CycleManager.h"
#include "../Machine/OEEManager.h"
#include "../Machine/ShiftManager.h"
#include "../Core/HardwareConfig.h"
#include "../Core/Logger.h"
#include <cstring>

namespace {
uint16_t gRegisters[HMIRegister::RegisterCount] = {0};
uint16_t gLastLossCode = 0;
uint16_t gLastCycleSeconds = 0;
uint16_t gLastTargetQuantity = 0;
uint16_t gLastShift = 0;
uint16_t gLastOperatorId = 0;
uint32_t gLastPartNumber = 0;
char gLastPartName[17] = "";
uint32_t gLastSyncMs = 0;
uint16_t gHeartbeat = 0;

void write32(uint16_t lowRegister, uint32_t value) {
  gRegisters[lowRegister] = static_cast<uint16_t>(value & 0xFFFFU);
  gRegisters[lowRegister + 1] = static_cast<uint16_t>(value >> 16U);
}

uint32_t read32(uint16_t lowRegister) {
  return static_cast<uint32_t>(gRegisters[lowRegister]) |
         (static_cast<uint32_t>(gRegisters[lowRegister + 1]) << 16U);
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
}

void HMIManager::begin() {
  RS485Driver::begin(HardwareConfig::Rs485DirectionPin, HardwareConfig::Rs485Baud);
  ModbusSlave::begin(HardwareConfig::ModbusSlaveId,
                     gRegisters,
                     HMIRegister::RegisterCount);
  gRegisters[HMIRegister::CommandCycleTimeSeconds] = static_cast<uint16_t>(CycleManager::cycleTimeMs() / 1000UL);
  gRegisters[HMIRegister::CommandShift] = 1;
  Logger::info(F("Delta HMI Modbus RTU hardware UART ready"));
}

void HMIManager::update() {
  ModbusSlave::update();

  const uint16_t lossCode = gRegisters[HMIRegister::CommandLossCode];
  if (lossCode != 0 && lossCode != gLastLossCode) {
    MachineEngine::acknowledgeLossCode(lossCode);
    gLastLossCode = lossCode;
    gRegisters[HMIRegister::CommandLossCode] = 0;
  }

  const uint16_t cycleSeconds = gRegisters[HMIRegister::CommandCycleTimeSeconds];
  if (cycleSeconds >= 1 && cycleSeconds <= 3600 && cycleSeconds != gLastCycleSeconds) {
    const uint32_t cycleMs = static_cast<uint32_t>(cycleSeconds) * 1000UL;
    CycleManager::setCycleTimeMs(cycleMs);
    OEEManager::setIdealCycleTimeMs(cycleMs);
    gLastCycleSeconds = cycleSeconds;
  }

  const uint16_t targetQuantity = gRegisters[HMIRegister::CommandTargetQuantity];
  if (targetQuantity != gLastTargetQuantity) {
    ShiftManager::setTargetQuantity(targetQuantity);
    gLastTargetQuantity = targetQuantity;
  }

  const uint16_t shift = gRegisters[HMIRegister::CommandShift];
  if (shift >= 1 && shift <= 3 && shift != gLastShift) {
    ShiftManager::setShift(shift);
    gLastShift = shift;
  }

  const uint16_t operatorId = gRegisters[HMIRegister::CommandOperatorId];
  if (operatorId != gLastOperatorId) {
    ShiftManager::setOperatorId(operatorId);
    gLastOperatorId = operatorId;
  }

  char partName[17];
  readPartName(partName, sizeof(partName));
  const uint32_t partNumber = read32(HMIRegister::CommandPartNumberLow);
  if (partNumber != gLastPartNumber || strncmp(partName, gLastPartName, sizeof(gLastPartName)) != 0) {
    ShiftManager::setPart(partNumber, partName);
    gLastPartNumber = partNumber;
    strncpy(gLastPartName, partName, sizeof(gLastPartName) - 1);
    gLastPartName[sizeof(gLastPartName) - 1] = '\0';
  }

  if (millis() - gLastSyncMs < 250UL) return;
  gLastSyncMs = millis();

  const MachineSnapshot machine = MachineEngine::snapshot();
  const ShiftSnapshot shiftData = ShiftManager::snapshot();
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
  write32(HMIRegister::StatusTargetQuantityLow, machine.targetQuantity);
  gRegisters[HMIRegister::StatusShift] = shiftData.shiftId;
  write32(HMIRegister::StatusOperatorIdLow, shiftData.operatorId);
  write32(HMIRegister::StatusPartNumberLow, shiftData.partNumber);
  write32(HMIRegister::StatusShiftProductionLow, shiftData.production);
  write32(HMIRegister::StatusShiftRejectLow, shiftData.reject);
  write32(HMIRegister::StatusShiftGoodLow, shiftData.good);
  const uint32_t remaining = shiftData.targetQuantity > shiftData.production
                                 ? shiftData.targetQuantity - shiftData.production
                                 : 0;
  write32(HMIRegister::StatusTargetRemainingLow, remaining);
}

bool HMIManager::connected() { return ModbusSlave::connected(); }
uint32_t HMIManager::requestCount() { return ModbusSlave::requestCount(); }
uint32_t HMIManager::errorCount() { return ModbusSlave::errorCount(); }