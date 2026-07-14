#pragma once
#include <Arduino.h>

namespace HMIRegister {
constexpr uint16_t CommandLossCode = 0;
constexpr uint16_t CommandCycleTimeSeconds = 1;
constexpr uint16_t CommandTargetQuantity = 2;
constexpr uint16_t CommandOperatorId = 3;
constexpr uint16_t CommandShift = 4;
constexpr uint16_t CommandHeartbeat = 5;

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

constexpr uint16_t RegisterCount = 64;
}
