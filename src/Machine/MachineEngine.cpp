#include "MachineEngine.h"
#include "ProductionManager.h"
#include "RejectManager.h"
#include "CycleManager.h"
#include "CycleEndManager.h"
#include "IdleManager.h"
#include "AlarmManager.h"
#include "OEEManager.h"
#include "ShiftManager.h"
#include "../Core/EventBus.h"
#include "../Core/Logger.h"
#include "../Core/HardwareConfig.h"
#include "../Core/PulseConfig.h"
#include "../Core/Config.h"

namespace {
bool gReady = false;
bool gHasProductionPulse = false;
bool gLossMonitoringActive = false;
MachineState gState = MachineState::Ready;
bool gWasIdle = false;
bool gWasAlarmActive = false;
bool gLossClassifiedForCurrentIdle = false;
uint32_t gLossStartIdleSeconds = 0;
uint16_t gLastAcceptedLossCode = 0;
uint32_t gLastLossDurationSeconds = 0;

bool occursBeforeOrSame(uint32_t first, uint32_t second) {
  return static_cast<int32_t>(first - second) <= 0;
}

uint32_t eventUsToMillis(uint32_t eventUs, uint32_t nowUs, uint32_t nowMs) {
  return nowMs - ((nowUs - eventUs) / 1000UL);
}

void resetIdleTracking() {
  IdleManager::onProduction();
  gLossClassifiedForCurrentIdle = false;
  gWasIdle = false;
}

void restartFixedTimeMonitoring(uint32_t nowMs) {
  // Fixed-time mode must wait for a real Cycle Start. Starting a synthetic
  // cycle here would reject the first operator/machine pulse as a duplicate.
  CycleManager::armWaitingForStart(nowMs);
  resetIdleTracking();
  gLossMonitoringActive = true;
}

void monitorWaitingForStart(uint32_t nowMs) {
  CycleManager::armWaitingForStart(nowMs);
  resetIdleTracking();
  gLossMonitoringActive = true;
}

void processCycleStart(uint32_t timestampMs) {
  if (!CycleManager::onProduction(timestampMs)) {
    Logger::warn(F("[CYCLE START] Duplicate start ignored while cycle active"));
    return;
  }

  ProductionManager::acceptPulse();
  gHasProductionPulse = true;
  gLossMonitoringActive = true;
  resetIdleTracking();
  if (!AlarmManager::active()) gState = MachineState::Running;
  EventBus::publish(EventType::ProductionPulse,
                    static_cast<int32_t>(ProductionManager::total()));
}

void processCycleEnd(uint32_t timestampMs) {
  if (CycleManager::onCycleEnd(timestampMs)) {
    Logger::info(F("[CYCLE END] Valid cycle completion received"));
  } else {
    Logger::warn(F("[CYCLE END] Ignored: no active cycle or interval too short"));
  }
}
}

void MachineEngine::begin() {
  PulseConfig::load();
  ProductionManager::begin(HardwareConfig::ProductionInputPin,
                           PulseConfig::productionDebounceMs() * 1000UL);
  ProductionManager::setEnabled(true);
  RejectManager::begin(HardwareConfig::RejectInputPin,
                       PulseConfig::rejectDebounceMs() * 1000UL);
  RejectManager::setEnabled(true);

  const bool useCycleEnd = Config::cycleEndEnabled();
  CycleManager::begin(HardwareConfig::DefaultCycleTimeMs,
                      useCycleEnd,
                      Config::cycleEndTimeoutSeconds() * 1000UL,
                      Config::cycleEndMinimumMs());
  if (useCycleEnd) {
    CycleEndManager::begin(HardwareConfig::CycleEndInputPin,
                           Config::cycleEndActiveHigh(),
                           Config::cycleEndDebounceMs());
  }

  IdleManager::begin(Config::lossAlarmDelaySeconds() * 1000UL);
  AlarmManager::begin(HardwareConfig::AlarmOutputPin, Config::alarmActiveHigh());
  OEEManager::begin(HardwareConfig::DefaultCycleTimeMs);

  gState = MachineState::Ready;
  gReady = true;
  gHasProductionPulse = false;
  if (useCycleEnd) monitorWaitingForStart(millis());
  else restartFixedTimeMonitoring(millis());

  EventBus::publish(EventType::MachineReady);
  Logger::info(String(F("[CYCLE] Completion mode: ")) +
               (useCycleEnd ? F("Cycle Start -> Cycle End") : F("Cycle Start -> fixed cycle time")));
  Logger::info(String(F("[LOSS] Idle alarm delay: ")) +
               Config::lossAlarmDelaySeconds() + F(" sec"));
  Logger::info(F("Machine engine ready"));
}

void MachineEngine::update() {
  const uint32_t nowMs = millis();
  const uint32_t nowUs = micros();

  uint32_t startTimeUs = 0;
  uint32_t endTimeUs = 0;
  bool hasStart = ProductionManager::consumePulse(startTimeUs);
  bool hasEnd = CycleManager::cycleEndEnabled() && CycleEndManager::consumePulse(endTimeUs);

  while (hasStart || hasEnd) {
    if (hasStart && (!hasEnd || occursBeforeOrSame(startTimeUs, endTimeUs))) {
      processCycleStart(eventUsToMillis(startTimeUs, nowUs, nowMs));
      hasStart = ProductionManager::consumePulse(startTimeUs);
    } else {
      processCycleEnd(eventUsToMillis(endTimeUs, nowUs, nowMs));
      hasEnd = CycleEndManager::consumePulse(endTimeUs);
    }
  }

  while (RejectManager::consumePulse()) {
    EventBus::publish(EventType::RejectPulse,
                      static_cast<int32_t>(RejectManager::total()));
  }

  CycleManager::update(nowMs);
  if (gLossMonitoringActive) {
    IdleManager::update(CycleManager::cycleCompleted(),
                        nowMs,
                        CycleManager::completionTimeMs());
  }

  const bool idleNow = gLossMonitoringActive && IdleManager::idle();
  const bool waitingForStart = CycleManager::completionReason() ==
                               CycleManager::CompletionReason::WaitingForStart;
  if (idleNow && !gWasIdle) {
    gLossStartIdleSeconds = IdleManager::idleSeconds();
    gLossClassifiedForCurrentIdle = false;
    if (!waitingForStart) {
      gState = MachineState::Idle;
      EventBus::publish(EventType::IdleStarted);
    }
    if (CycleManager::completionReason() == CycleManager::CompletionReason::CycleEndTimeout) {
      Logger::warn(F("[CYCLE END] Timeout completed the cycle; idle timing started"));
    }
  } else if (!idleNow && gWasIdle && !waitingForStart) {
    EventBus::publish(EventType::IdleEnded);
  }
  gWasIdle = idleNow;

  if (gLossMonitoringActive && IdleManager::alarmDue() && !gLossClassifiedForCurrentIdle) {
    if (!AlarmManager::active()) {
      AlarmManager::set(true);
      ProductionManager::setEnabled(false);
      RejectManager::setEnabled(false);
      if (CycleManager::cycleEndEnabled()) CycleEndManager::setEnabled(false);
      Logger::warn(F("[LOSS] Production, reject and cycle end capture blocked until loss is acknowledged"));
    }
    gState = MachineState::LossRequired;
  }

  const bool alarmNow = AlarmManager::active();
  if (alarmNow && !gWasAlarmActive) EventBus::publish(EventType::AlarmActivated);
  if (!alarmNow && gWasAlarmActive) EventBus::publish(EventType::AlarmCleared);
  gWasAlarmActive = alarmNow;

  const bool oeeDowntime = !gHasProductionPulse || idleNow || alarmNow;
  OEEManager::update(oeeDowntime,
                     ProductionManager::total(),
                     RejectManager::total());
}

bool MachineEngine::ready() { return gReady; }

MachineSnapshot MachineEngine::snapshot() {
  const uint32_t total = ProductionManager::total();
  const uint32_t rejects = RejectManager::total();
  const OEESnapshot oee = OEEManager::snapshot();
  const ShiftSnapshot shift = ShiftManager::snapshot();
  MachineSnapshot value;
  value.state = gState;
  value.totalParts = total;
  value.rejectParts = rejects;
  value.goodParts = total > rejects ? total - rejects : 0;
  value.lastProductionMs = CycleManager::lastProductionMs();
  value.idleSeconds = IdleManager::idleSeconds();
  value.runSeconds = oee.runSeconds;
  value.downtimeSeconds = oee.downtimeSeconds;
  value.targetQuantity = shift.targetQuantity;
  value.availabilityPermille = oee.availabilityPermille;
  value.performancePermille = oee.performancePermille;
  value.qualityPermille = oee.qualityPermille;
  value.oeePermille = oee.oeePermille;
  value.alarmActive = AlarmManager::active();
  return value;
}

bool MachineEngine::acknowledgeLossCode(uint16_t lossCode) {
  if (lossCode == 0 || lossCode > 16 || !AlarmManager::active() ||
      gLossClassifiedForCurrentIdle) {
    return false;
  }

  const uint32_t idleNow = IdleManager::idleSeconds();
  const uint32_t duration = idleNow >= gLossStartIdleSeconds
                                ? idleNow - gLossStartIdleSeconds
                                : 0;
  OEEManager::recordLoss(lossCode, duration);
  EventBus::publish(EventType::LossSelected, lossCode, duration);

  gLossClassifiedForCurrentIdle = true;
  AlarmManager::clear();
  ProductionManager::setEnabled(true);
  RejectManager::setEnabled(true);
  gHasProductionPulse = false;
  if (CycleManager::cycleEndEnabled()) {
    CycleEndManager::setEnabled(true);
    CycleEndManager::clearPending();
    monitorWaitingForStart(millis());
  } else {
    restartFixedTimeMonitoring(millis());
  }
  gState = MachineState::Ready;
  gLastAcceptedLossCode = lossCode;
  gLastLossDurationSeconds = duration;
  Logger::info(F("[LOSS] Loss captured; machine ready and next-start monitoring active"));
  return true;
}

uint16_t MachineEngine::lastAcceptedLossCode() { return gLastAcceptedLossCode; }
uint32_t MachineEngine::lastLossDurationSeconds() { return gLastLossDurationSeconds; }
void MachineEngine::restoreLastLoss(uint16_t lossCode, uint32_t durationSeconds) {
  gLastAcceptedLossCode = lossCode <= 16 ? lossCode : 0;
  gLastLossDurationSeconds = durationSeconds;
}
