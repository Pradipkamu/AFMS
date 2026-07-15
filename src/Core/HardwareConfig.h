#pragma once
#include <Arduino.h>

namespace HardwareConfig {
constexpr uint8_t ProductionInputPin = D2;
constexpr uint8_t RejectInputPin = D1;
constexpr uint8_t AlarmOutputPin = D5;

// MAX485 / Delta HMI test wiring.
// RO -> D7 (ESP8266 receive)
// DI -> D8 (ESP8266 transmit)
// DE and /RE tied together -> D6 (direction control)
constexpr uint8_t Rs485RxPin = D7;
constexpr uint8_t Rs485TxPin = D8;
constexpr uint8_t Rs485DirectionPin = D6;

constexpr uint8_t ModbusSlaveId = 1;
constexpr uint32_t Rs485Baud = 9600UL;
constexpr uint32_t InputDebounceUs = 50000UL;
constexpr uint32_t DefaultCycleTimeMs = 20000UL;
constexpr uint32_t DefaultIdleDelayMs = 600000UL;
}