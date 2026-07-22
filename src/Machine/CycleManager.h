#pragma once
#include <Arduino.h>

namespace CycleManager {
enum class CompletionReason : uint8_t {
  None = 0,
  FixedTime = 1,
  CycleEndInput = 2,
  CycleEndTimeout = 3,
  WaitingForStart = 4
};

void begin(uint32_t cycleTimeMs,
           bool cycleEndEnabled,
           uint32_t cycleEndTimeoutMs,
           uint32_t cycleEndMinimumMs);
void setCycleTimeMs(uint32_t cycleTimeMs);
bool onProduction(uint32_t nowMs);
bool onCycleEnd(uint32_t nowMs);
void armWaitingForStart(uint32_t nowMs);
void update(uint32_t nowMs);
uint32_t cycleTimeMs();
uint32_t lastProductionMs();
uint32_t completionTimeMs();
bool cycleCompleted();
bool cycleInProgress();
bool cycleEndEnabled();
CompletionReason completionReason();
uint32_t timeoutCount();
uint32_t duplicateStartCount();
uint32_t earlyCycleEndCount();
}
