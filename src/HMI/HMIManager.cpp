#include "HMIManager.h"
#include "RegisterMap.h"
#include "../Communication/RS485Driver.h"
#include "../Communication/ModbusSlave.h"
#include "../Machine/MachineEngine.h"
#include "../Machine/CycleManager.h"
#include "../Core/HardwareConfig.h"
#include "../Core/Logger.h"

namespace {
uint16_t gRegisters[HMIRegister::RegisterCount] = {0};
uint16_t gLastLossCode = 0;
uint16_t gLastCycleSeconds = 0;
uint32_t gLastSyncMs = 0;
uint16_t gHeartbeat = 0;

void write32(uint16_t lowRegister, uint32_t value) {
  gRegisters[lowRegister] = static_cast<uint16_t>(value & 0xFFFFU);
  gRegisters[lowRegister + 1] = static_cast<uint16_t>(value >> 16U);
}
}

void HMIManager::begin() {
  RS485Driver::begin(HardwareConfig::Rs485RxPin,
                    HardwareConfig::Rs485TxPin,
                    HardwareConfig::Rs485DirectionPin,
                    HardwareConfig::Rs485Baud);
  ModbusSlave::begin(HardwareConfig::ModbusSlaveId,
                     gRegisters,
                     HMIRegister::RegisterCount);
  gRegisters[HMIRegister::CommandCycleTimeSeconds] = static_cast<uint16_t>(CycleManager::cycleTimeMs() / 1000UL);
  Logger::info(F("Delta HMI Modbus RTU ready"));
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
    CycleManager::setCycleTimeMs(static_cast<uint32_t>(cycleSeconds) * 1000UL);
    gLastCycleSeconds = cycleSeconds;
  }

  if (millis() - gLastSyncMs < 250UL) return;
  gLastSyncMs = millis();

  const MachineSnapshot snapshot = MachineEngine::snapshot();
  gRegisters[HMIRegister::StatusMachineState] = static_cast<uint16_t>(snapshot.state);
  write32(HMIRegister::StatusProductionLow, snapshot.totalParts);
  write32(HMIRegister::StatusRejectLow, snapshot.rejectParts);
  write32(HMIRegister::StatusGoodLow, snapshot.goodParts);
  write32(HMIRegister::StatusIdleSecondsLow, snapshot.idleSeconds);
  gRegisters[HMIRegister::StatusAlarm] = snapshot.alarmActive ? 1 : 0;
  gRegisters[HMIRegister::StatusCommunication] = ModbusSlave::connected() ? 1 : 0;
  gRegisters[HMIRegister::StatusEspHeartbeat] = ++gHeartbeat;
}

bool HMIManager::connected() { return ModbusSlave::connected(); }
uint32_t HMIManager::requestCount() { return ModbusSlave::requestCount(); }
uint32_t HMIManager::errorCount() { return ModbusSlave::errorCount(); }
