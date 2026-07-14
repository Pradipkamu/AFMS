#pragma once
#include <Arduino.h>

namespace HardwareConfig {
constexpr uint8_t ProductionInputPin = D2;
constexpr uint8_t RejectInputPin = D1;
constexpr uint8_t AlarmOutputPin = D5;
constexpr uint8_t Rs485RxPin = D6;
constexpr uint8_t Rs485TxPin = D7;
constexpr uint8_t Rs485DirectionPin = D8;
constexpr uint8_t ModbusSlaveId = 1;
constexpr uint32_t Rs485Baud = 9600UL;
constexpr uint32_t InputDebounceUs = 50000UL;
constexpr uint32_t DefaultCycleTimeMs = 20000UL;
constexpr uint32_t DefaultIdleDelayMs = 600000UL;
}
