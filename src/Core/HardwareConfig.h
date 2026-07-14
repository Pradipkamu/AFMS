#pragma once
#include <Arduino.h>

namespace HardwareConfig {
constexpr uint8_t ProductionInputPin = D2;
constexpr uint8_t RejectInputPin = D1;
constexpr uint8_t AlarmOutputPin = D5;
constexpr uint32_t InputDebounceUs = 50000UL;
constexpr uint32_t DefaultCycleTimeMs = 20000UL;
constexpr uint32_t DefaultIdleDelayMs = 600000UL;
}
