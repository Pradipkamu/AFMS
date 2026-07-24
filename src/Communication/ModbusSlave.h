#pragma once
#include <Arduino.h>

namespace ModbusSlave {
void begin(uint8_t slaveId, uint16_t *registers, uint16_t registerCount);
void update();
bool consumeCoil(uint16_t address);
bool coil(uint16_t address);
bool connected();
uint32_t requestCount();
uint32_t errorCount();
uint32_t lastRequestMs();

// Compatibility helper used by HMI diagnostics. Returns zero until the first
// valid Modbus request, then the elapsed milliseconds since that request.
inline uint32_t lastRequestAgeMs() {
  const uint32_t last = lastRequestMs();
  return last == 0 ? 0 : millis() - last;
}
}