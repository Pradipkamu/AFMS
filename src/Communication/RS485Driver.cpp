#include "RS485Driver.h"

namespace {
SoftwareSerial gPort;
uint8_t gDirectionPin = 255;
}

void RS485Driver::begin(uint8_t rxPin, uint8_t txPin, uint8_t directionPin, uint32_t baud) {
  gDirectionPin = directionPin;
  pinMode(gDirectionPin, OUTPUT);
  digitalWrite(gDirectionPin, LOW);
  gPort.begin(baud, SWSERIAL_8N1, rxPin, txPin, false, 128);
  gPort.enableIntTx(false);
}

SoftwareSerial &RS485Driver::port() { return gPort; }

void RS485Driver::setTransmit(bool enabled) {
  digitalWrite(gDirectionPin, enabled ? HIGH : LOW);
  if (enabled) delayMicroseconds(200);
}

void RS485Driver::flushInput() {
  while (gPort.available()) gPort.read();
}
