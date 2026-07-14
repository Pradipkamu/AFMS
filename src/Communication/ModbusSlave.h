#pragma once
#include <Arduino.h>

namespace ModbusSlave {
void begin(uint8_t slaveId, uint16_t *registers, uint16_t registerCount);
void update();
bool connected();
uint32_t requestCount();
uint32_t errorCount();
uint32_t lastRequestMs();
}
