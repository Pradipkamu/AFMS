#include "ProductionIO.h"
#include "../Core/HardwareConfig.h"
#include "../Core/Logger.h"

namespace {

struct InputChannel {
  uint8_t pin = 0;
  bool raw = false;
  bool stable = false;
  uint32_t rawChangedMs = 0;
  uint32_t stableChangedMs = 0;
  uint32_t changeCount = 0;
};

struct OutputChannel {
  uint8_t pin = 0;
  bool actual = false;
  uint32_t changedMs = 0;
  uint32_t changeCount = 0;
};

InputChannel gInputs[static_cast<uint8_t>(ProductionIO::InputId::Count)];
OutputChannel gOutputs[static_cast<uint8_t>(ProductionIO::OutputId::Count)];
bool gReady = false;
constexpr uint32_t kPollIntervalMs = 5UL;
uint32_t gLastPollMs = 0;

InputChannel &inputChannel(ProductionIO::InputId id) {
  return gInputs[static_cast<uint8_t>(id)];
}

OutputChannel &outputChannel(ProductionIO::OutputId id) {
  return gOutputs[static_cast<uint8_t>(id)];
}

void initializeInput(InputChannel &channel, uint8_t pin) {
  channel.pin = pin;
  pinMode(pin, INPUT_PULLUP);
  channel.raw = digitalRead(pin) == HIGH;
  channel.stable = channel.raw;
  channel.rawChangedMs = millis();
  channel.stableChangedMs = channel.rawChangedMs;
  channel.changeCount = 0;
}

void initializeOutput(OutputChannel &channel, uint8_t pin) {
  channel.pin = pin;
  channel.actual = digitalRead(pin) == HIGH;
  channel.changedMs = millis();
  channel.changeCount = 0;
}

void updateInput(InputChannel &channel, uint32_t nowMs) {
  const bool rawNow = digitalRead(channel.pin) == HIGH;
  if (rawNow != channel.raw) {
    channel.raw = rawNow;
    channel.rawChangedMs = nowMs;
  }

  const uint32_t debounceMs = HardwareConfig::InputDebounceUs / 1000UL;
  if (channel.stable != channel.raw && nowMs - channel.rawChangedMs >= debounceMs) {
    channel.stable = channel.raw;
    channel.stableChangedMs = nowMs;
    ++channel.changeCount;
  }
}

void updateOutput(OutputChannel &channel, uint32_t nowMs) {
  const bool actualNow = digitalRead(channel.pin) == HIGH;
  if (actualNow != channel.actual) {
    channel.actual = actualNow;
    channel.changedMs = nowMs;
    ++channel.changeCount;
  }
}

ProductionIO::InputSnapshot makeInputSnapshot(const InputChannel &channel) {
  ProductionIO::InputSnapshot value;
  value.raw = channel.raw;
  value.stable = channel.stable;
  value.changeCount = channel.changeCount;
  value.lastChangeMs = channel.stableChangedMs;
  return value;
}

ProductionIO::OutputSnapshot makeOutputSnapshot(const OutputChannel &channel) {
  ProductionIO::OutputSnapshot value;
  value.actual = channel.actual;
  value.changeCount = channel.changeCount;
  value.lastChangeMs = channel.changedMs;
  return value;
}

}  // namespace

void ProductionIO::begin() {
  initializeInput(inputChannel(InputId::Production), HardwareConfig::ProductionInputPin);
  initializeInput(inputChannel(InputId::Reject), HardwareConfig::RejectInputPin);
  initializeOutput(outputChannel(OutputId::Alarm), HardwareConfig::AlarmOutputPin);
  gLastPollMs = millis();
  gReady = true;
  Logger::info(F("[ProductionIO] diagnostics layer ready"));
}

void ProductionIO::update() {
  if (!gReady) return;
  const uint32_t nowMs = millis();
  if (nowMs - gLastPollMs < kPollIntervalMs) return;
  gLastPollMs = nowMs;

  updateInput(inputChannel(InputId::Production), nowMs);
  updateInput(inputChannel(InputId::Reject), nowMs);
  updateOutput(outputChannel(OutputId::Alarm), nowMs);
}

ProductionIO::InputSnapshot ProductionIO::input(InputId id) {
  if (id >= InputId::Count) return InputSnapshot();
  return makeInputSnapshot(inputChannel(id));
}

ProductionIO::OutputSnapshot ProductionIO::output(OutputId id) {
  if (id >= OutputId::Count) return OutputSnapshot();
  return makeOutputSnapshot(outputChannel(id));
}

ProductionIO::Snapshot ProductionIO::snapshot() {
  Snapshot value;
  value.production = input(InputId::Production);
  value.reject = input(InputId::Reject);
  value.alarm = output(OutputId::Alarm);
  return value;
}

bool ProductionIO::ready() { return gReady; }
