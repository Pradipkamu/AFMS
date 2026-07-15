#pragma once
#include <Arduino.h>

namespace RS485Driver {
void begin(uint8_t directionPin, uint32_t baud);
HardwareSerial &port();
void setTransmit(bool enabled);
void flushInput();
}