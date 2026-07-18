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

constexpr uint16_t kCoilCount = 16;
bool gCoils[kCoilCount] = {false};
constexpr uint16_t kWritableRegisterCount = 16;
constexpr uint16_t kLossCommandOffset = 0;
constexpr uint16_t kLossCommandAliasOffset = 94;
constexpr uint32_t kFrameGapUs = 4500UL;

bool writableAddress(uint16_t address) {
  return address < kWritableRegisterCount || address == kLossCommandAliasOffset;
}

bool writableRange(uint16_t address, uint16_t quantity) {
  if (quantity == 0) return false;
  if (address < kWritableRegisterCount && address + quantity <= kWritableRegisterCount) return true;
  return address == kLossCommandAliasOffset && quantity == 1;
}

void storeRegister(uint16_t address, uint16_t value) {
  gRegisters[address] = value;
  if (address == kLossCommandAliasOffset) gRegisters[kLossCommandOffset] = value;
}

void storeCoil(uint16_t address, bool value) {
  if (address >= kCoilCount) return;
  gCoils[address] = value;
  if (value && gRegisters && gRegisters[kLossCommandOffset] == 0) {
    gRegisters[kLossCommandOffset] = static_cast<uint16_t>(address + 1U);
  }
}

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

uint16_t expectedFrameLength() {
  if (gLength < 2) return 0;

  switch (gFrame[1]) {
    case 0x01:
    case 0x03:
    case 0x05:
    case 0x06:
      return 8;

    case 0x0F:
    case 0x10:
      if (gLength < 7) return 0;
      return static_cast<uint16_t>(9U + gFrame[6]);

    default:
      return 8;
  }
}

void resetReceiveFrame(bool countError) {
  if (countError) ++gErrors;
  gLength = 0;
  gLastByteUs = 0;
}

void processFrame() {
  if (gLength < 8 || gFrame[0] != gSlaveId) return;
  const uint16_t received = static_cast<uint16_t>(gFrame[gLength - 2]) |
                            static_cast<uint16_t>(gFrame[gLength - 1] << 8U);
  if (crc16(gFrame, gLength - 2) != received) { ++gErrors; return; }

  const uint8_t functionCode = gFrame[1];
  const uint16_t address = static_cast<uint16_t>(gFrame[2] << 8U) | gFrame[3];
  const uint16_t quantityOrValue = static_cast<uint16_t>(gFrame[4] << 8U) | gFrame[5];

  if (functionCode == 0x01) {
    const uint16_t quantity = quantityOrValue;
    if (quantity == 0 || quantity > kCoilCount || address + quantity > kCoilCount) {
      exception(functionCode, 0x02);
      return;
    }
    const uint8_t byteCount = static_cast<uint8_t>((quantity + 7U) / 8U);
    uint8_t reply[7] = {gSlaveId, functionCode, byteCount, 0, 0, 0, 0};
    for (uint16_t i = 0; i < quantity; ++i) {
      if (gCoils[address + i]) reply[3 + i / 8U] |= static_cast<uint8_t>(1U << (i % 8U));
    }
    sendFrame(reply, static_cast<uint8_t>(3 + byteCount));
  } else if (functionCode == 0x03) {
    const uint16_t quantity = quantityOrValue;
    if (quantity == 0 || quantity > 32 || address + quantity > gRegisterCount) {
      exception(functionCode, 0x02);
      return;
    }
    uint8_t reply[70];
    reply[0] = gSlaveId;
    reply[1] = functionCode;
    reply[2] = static_cast<uint8_t>(quantity * 2U);
    for (uint16_t i = 0; i < quantity; ++i) {
      reply[3 + i * 2] = static_cast<uint8_t>(gRegisters[address + i] >> 8U);
      reply[4 + i * 2] = static_cast<uint8_t>(gRegisters[address + i] & 0xFFU);
    }
    sendFrame(reply, static_cast<uint8_t>(3 + quantity * 2U));
  } else if (functionCode == 0x05) {
    if (address >= kCoilCount || (quantityOrValue != 0xFF00U && quantityOrValue != 0x0000U)) {
      exception(functionCode, 0x02);
      return;
    }
    storeCoil(address, quantityOrValue == 0xFF00U);
    uint8_t reply[8];
    memcpy(reply, gFrame, 6);
    sendFrame(reply, 6);
  } else if (functionCode == 0x06) {
    if (!writableAddress(address) || address >= gRegisterCount) {
      exception(functionCode, 0x02);
      return;
    }
    storeRegister(address, quantityOrValue);
    uint8_t reply[8];
    memcpy(reply, gFrame, 6);
    sendFrame(reply, 6);
  } else if (functionCode == 0x0F) {
    const uint16_t quantity = quantityOrValue;
    const uint8_t byteCount = gFrame[6];
    if (quantity == 0 || address + quantity > kCoilCount ||
        byteCount != static_cast<uint8_t>((quantity + 7U) / 8U) ||
        gLength < static_cast<uint16_t>(9U + byteCount)) {
      exception(functionCode, 0x03);
      return;
    }
    for (uint16_t i = 0; i < quantity; ++i) {
      storeCoil(address + i,
                (gFrame[7 + i / 8U] & static_cast<uint8_t>(1U << (i % 8U))) != 0);
    }
    uint8_t reply[8] = {gSlaveId, functionCode, gFrame[2], gFrame[3], gFrame[4], gFrame[5], 0, 0};
    sendFrame(reply, 6);
  } else if (functionCode == 0x10) {
    const uint16_t quantity = quantityOrValue;
    if (gLength < 9 || !writableRange(address, quantity) ||
        address + quantity > gRegisterCount || gFrame[6] != quantity * 2U) {
      exception(functionCode, 0x03);
      return;
    }
    for (uint16_t i = 0; i < quantity; ++i) {
      const uint16_t value = static_cast<uint16_t>(gFrame[7 + i * 2] << 8U) | gFrame[8 + i * 2];
      storeRegister(address + i, value);
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
  gLastByteUs = 0;
  for (uint16_t i = 0; i < kCoilCount; ++i) gCoils[i] = false;
}

void ModbusSlave::update() {
  HardwareSerial &serial = RS485Driver::port();

  while (serial.available()) {
    const int value = serial.read();
    if (value < 0) continue;

    if (gLength >= sizeof(gFrame)) {
      resetReceiveFrame(true);
      RS485Driver::flushInput();
      break;
    }

    gFrame[gLength++] = static_cast<uint8_t>(value);
    gLastByteUs = micros();

    const uint16_t expected = expectedFrameLength();
    if (expected > sizeof(gFrame)) {
      resetReceiveFrame(true);
      RS485Driver::flushInput();
      break;
    }

    if (expected != 0 && gLength == expected) {
      processFrame();
      resetReceiveFrame(false);
    } else if (expected != 0 && gLength > expected) {
      resetReceiveFrame(true);
    }
  }

  if (gLength > 0 && static_cast<uint32_t>(micros() - gLastByteUs) > kFrameGapUs) {
    processFrame();
    resetReceiveFrame(false);
  }
}

bool ModbusSlave::consumeCoil(uint16_t address) {
  if (address >= kCoilCount || !gCoils[address]) return false;
  gCoils[address] = false;
  return true;
}

bool ModbusSlave::coil(uint16_t address) {
  return address < kCoilCount && gCoils[address];
}

bool ModbusSlave::connected() { return gLastRequest != 0 && millis() - gLastRequest < 5000UL; }
uint32_t ModbusSlave::requestCount() { return gRequests; }
uint32_t ModbusSlave::errorCount() { return gErrors; }
uint32_t ModbusSlave::lastRequestMs() { return gLastRequest; }
