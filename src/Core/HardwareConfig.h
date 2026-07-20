#pragma once
#include <Arduino.h>

namespace HardwareConfig {
constexpr uint8_t ProductionInputPin = D2;
constexpr uint8_t RejectInputPin = D1;
constexpr uint8_t AlarmOutputPin = D5;

// ESP8266 UART0 hardware pins: RX0=GPIO3, TX0=GPIO1.
constexpr uint8_t Rs485RxPin = 3;
constexpr uint8_t Rs485TxPin = 1;
constexpr uint8_t Rs485DirectionPin = D6;
constexpr uint8_t ModbusSlaveId = 1;
constexpr uint32_t Rs485Baud = 9600UL;

// UART1 is TX-only on GPIO2 and carries commissioning diagnostics.
constexpr uint32_t DiagnosticBaud = 115200UL;

constexpr uint32_t InputDebounceUs = 50000UL;
constexpr uint32_t DefaultCycleTimeMs = 60000UL;
constexpr uint32_t DefaultIdleDelayMs = 600000UL;
}