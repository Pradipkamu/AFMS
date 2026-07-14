#pragma once
#include <Arduino.h>
#include <SoftwareSerial.h>

namespace RS485Driver {
void begin(uint8_t rxPin, uint8_t txPin, uint8_t directionPin, uint32_t baud);
SoftwareSerial &port();
void setTransmit(bool enabled);
void flushInput();
}
