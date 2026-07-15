#include "ModbusSlave.h"
#include "RS485Driver.h"

namespace {
uint8_t gSlaveId = 1;
uint16_t *gRegisters = nullptr;
uint16_t gRegisterCount = 0;
uint32_t gRequests = 0;
uint32_t gErrors = 0;
uint32_t gLastRequest = 0;
uint8_t gFrame[128];
uint8_t gLength = 0;
uint32_t gLastByteUs = 0;

// Holding-register offsets 0..15 are HMI command registers.
// Offsets 16 and above are AFMS status registers and are read-only.
constexpr uint16_t kWritableRegisterCount = 16;
constexpr uint32_t kFrameGapUs = 4500UL;  // 3.5+ characters at fixed 9600 baud, 8N1.

uint16_t crc16(const uint8_t *data, uint16_t length) {
  uint16_t crc = 0xFFFF;
  for (uint16_t i = 0; i < length; ++i) {
    crc ^= data[i];
    for (uint8_t bit = 0; bit < 8; ++bit) crc = (crc & 1U) ? (crc >> 1U) ^ 0xA001U : crc >> 1U;
  }
  return crc;
}

void sendFrame(uint8_t *data, uint8_t length) {
  const uint16_t crc = crc16(data, length);
  data[length++] = static_cast<uint8_t>(crc & 0xFFU);
  data[length++] = static_cast<uint8_t>(crc >> 8U);
  RS485Driver::setTransmit(true);
  RS485Driver::port().write(data, length);
  RS485Driver::port().flush();
  delayMicroseconds(250);
  RS485Driver::setTransmit(false);
}

void exception(uint8_t functionCode, uint8_t code) {
  uint8_t reply[5] = {gSlaveId, static_cast<uint8_t>(functionCode | 0x80U), code, 0, 0};
  sendFrame(reply, 3);
}

void processFrame() {
  if (gLength < 8 || gFrame[0] != gSlaveId) return;
  const uint16_t received = static_cast<uint16_t>(gFrame[gLength - 2]) | static_cast<uint16_t>(gFrame[gLength - 1] << 8U);
  if (crc16(gFrame, gLength - 2) != received) { ++gErrors; return; }

  const uint8_t functionCode = gFrame[1];
  const uint16_t address = static_cast<uint16_t>(gFrame[2] << 8U) | gFrame[3];
  const uint16_t quantityOrValue = static_cast<uint16_t>(gFrame[4] << 8U) | gFrame[5];

  if (functionCode == 0x03) {
    const uint16_t quantity = quantityOrValue;
    if (quantity == 0 || quantity > 32 || address + quantity > gRegisterCount) { exception(functionCode, 0x02); return; }
    uint8_t reply[70];
    reply[0] = gSlaveId;
    reply[1] = functionCode;
    reply[2] = static_cast<uint8_t>(quantity * 2U);
    for (uint16_t i = 0; i < quantity; ++i) {
      reply[3 + i * 2] = static_cast<uint8_t>(gRegisters[address + i] >> 8U);
      reply[4 + i * 2] = static_cast<uint8_t>(gRegisters[address + i] & 0xFFU);
    }
    sendFrame(reply, static_cast<uint8_t>(3 + quantity * 2U));
  } else if (functionCode == 0x06) {
    if (address >= kWritableRegisterCount || address >= gRegisterCount) { exception(functionCode, 0x02); return; }
    gRegisters[address] = quantityOrValue;
    uint8_t reply[8];
    memcpy(reply, gFrame, 6);
    sendFrame(reply, 6);
  } else if (functionCode == 0x10) {
    const uint16_t quantity = quantityOrValue;
    if (gLength < 9 || quantity == 0 || address + quantity > kWritableRegisterCount ||
        address + quantity > gRegisterCount || gFrame[6] != quantity * 2U) {
      exception(functionCode, 0x03);
      return;
    }
    for (uint16_t i = 0; i < quantity; ++i) {
      gRegisters[address + i] = static_cast<uint16_t>(gFrame[7 + i * 2] << 8U) | gFrame[8 + i * 2];
    }
    uint8_t reply[8] = {gSlaveId, functionCode, gFrame[2], gFrame[3], gFrame[4], gFrame[5], 0, 0};
    sendFrame(reply, 6);
  } else {
    exception(functionCode, 0x01);
    return;
  }

  ++gRequests;
  gLastRequest = millis();
}
}

void ModbusSlave::begin(uint8_t slaveId, uint16_t *registers, uint16_t registerCount) {
  gSlaveId = slaveId;
  gRegisters = registers;
  gRegisterCount = registerCount;
  gLength = 0;
}

void ModbusSlave::update() {
  HardwareSerial &serial = RS485Driver::port();
  while (serial.available()) {
    const int value = serial.read();
    if (value >= 0 && gLength < sizeof(gFrame)) gFrame[gLength++] = static_cast<uint8_t>(value);
    gLastByteUs = micros();
  }
  if (gLength > 0 && static_cast<uint32_t>(micros() - gLastByteUs) > kFrameGapUs) {
    processFrame();
    gLength = 0;
  }
}

bool ModbusSlave::connected() { return gLastRequest != 0 && millis() - gLastRequest < 5000UL; }
uint32_t ModbusSlave::requestCount() { return gRequests; }
uint32_t ModbusSlave::errorCount() { return gErrors; }
uint32_t ModbusSlave::lastRequestMs() { return gLastRequest; }
