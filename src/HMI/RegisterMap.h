#pragma once
#include <Arduino.h>

namespace HMIRegister {
constexpr uint16_t CommandLossCode = 0;
constexpr uint16_t CommandCycleTimeSeconds = 1;
constexpr uint16_t CommandTargetQuantity = 2;
constexpr uint16_t CommandOperatorId = 3;
constexpr uint16_t CommandShift = 4;
constexpr uint16_t CommandHeartbeat = 5;
constexpr uint16_t CommandPartNumberLow = 6;
constexpr uint16_t CommandPartNumberHigh = 7;
constexpr uint16_t CommandPartNameStart = 8;   // 8 registers, 16 ASCII characters
constexpr uint16_t CommandPartNameRegisters = 8;

constexpr uint16_t StatusMachineState = 16;
constexpr uint16_t StatusProductionLow = 17;
constexpr uint16_t StatusProductionHigh = 18;
constexpr uint16_t StatusRejectLow = 19;
constexpr uint16_t StatusRejectHigh = 20;
constexpr uint16_t StatusGoodLow = 21;
constexpr uint16_t StatusGoodHigh = 22;
constexpr uint16_t StatusIdleSecondsLow = 23;
constexpr uint16_t StatusIdleSecondsHigh = 24;
constexpr uint16_t StatusAlarm = 25;
constexpr uint16_t StatusCommunication = 26;
constexpr uint16_t StatusEspHeartbeat = 27;
constexpr uint16_t StatusRunSecondsLow = 28;
constexpr uint16_t StatusRunSecondsHigh = 29;
constexpr uint16_t StatusDowntimeSecondsLow = 30;
constexpr uint16_t StatusDowntimeSecondsHigh = 31;
constexpr uint16_t StatusAvailabilityPermille = 32;
constexpr uint16_t StatusPerformancePermille = 33;
constexpr uint16_t StatusQualityPermille = 34;
constexpr uint16_t StatusOeePermille = 35;
constexpr uint16_t StatusTargetQuantityLow = 36;
constexpr uint16_t StatusTargetQuantityHigh = 37;
constexpr uint16_t StatusShift = 38;
constexpr uint16_t StatusOperatorIdLow = 39;
constexpr uint16_t StatusOperatorIdHigh = 40;
constexpr uint16_t StatusPartNumberLow = 41;
constexpr uint16_t StatusPartNumberHigh = 42;
constexpr uint16_t StatusShiftProductionLow = 43;
constexpr uint16_t StatusShiftProductionHigh = 44;
constexpr uint16_t StatusShiftRejectLow = 45;
constexpr uint16_t StatusShiftRejectHigh = 46;
constexpr uint16_t StatusShiftGoodLow = 47;
constexpr uint16_t StatusShiftGoodHigh = 48;
constexpr uint16_t StatusTargetRemainingLow = 49;
constexpr uint16_t StatusTargetRemainingHigh = 50;
constexpr uint16_t StatusWifiConnected = 51;
constexpr uint16_t StatusGoogleConnected = 52;
constexpr uint16_t StatusTelegramConnected = 53;
constexpr uint16_t StatusOfflineQueueCount = 54;
constexpr uint16_t StatusGoogleSuccessLow = 55;
constexpr uint16_t StatusGoogleSuccessHigh = 56;
constexpr uint16_t StatusGoogleFailureLow = 57;
constexpr uint16_t StatusGoogleFailureHigh = 58;
constexpr uint16_t StatusTelegramSuccessLow = 59;
constexpr uint16_t StatusTelegramSuccessHigh = 60;
constexpr uint16_t StatusTelegramFailureLow = 61;
constexpr uint16_t StatusTelegramFailureHigh = 62;
constexpr uint16_t StatusModbusErrorCount = 63;

constexpr uint16_t RegisterCount = 64;
}