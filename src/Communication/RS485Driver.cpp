#include "RS485Driver.h"

namespace {
uint8_t gDirectionPin = 255;
}

void RS485Driver::begin(uint8_t directionPin, uint32_t baud) {
  gDirectionPin = directionPin;
  pinMode(gDirectionPin, OUTPUT);
  digitalWrite(gDirectionPin, LOW);

  Serial.end();
  Serial.begin(baud, SERIAL_8N1);
  Serial.setRxBufferSize(256);
  flushInput();
}

HardwareSerial &RS485Driver::port() { return Serial; }

void RS485Driver::setTransmit(bool enabled) {
  digitalWrite(gDirectionPin, enabled ? HIGH : LOW);
  if (enabled) delayMicroseconds(200);
}

void RS485Driver::flushInput() {
  while (Serial.available()) Serial.read();
}