#include "MachineEngine.h"
#include "ProductionManager.h"
#include "RejectManager.h"
#include "CycleManager.h"
#include "IdleManager.h"
#include "AlarmManager.h"
#include "OEEManager.h"
#include "../Core/EventBus.h"
#include "../Core/Logger.h"
#include "../Core/HardwareConfig.h"
#include "../Core/PulseConfig.h"
#include "../Core/Config.h"
#include "../Communication/TelegramClient.h"

namespace {
bool gReady = false;
bool gHasProductionPulse = false;
MachineState gState = MachineState::Ready;
bool gWasIdle = false;
bool gWasAlarmActive = false;
bool gLossClassifiedForCurrentIdle = false;
uint32_t gLossStartIdleSeconds = 0;
uint16_t gLastAcceptedLossCode = 0;
uint32_t gLastLossDurationSeconds = 0;
}

void MachineEngine::begin() {
  PulseConfig::load();
  ProductionManager::begin(HardwareConfig::ProductionInputPin,
                           PulseConfig::productionDebounceMs() * 1000UL);
  ProductionManager::setEnabled(true);
  RejectManager::begin(HardwareConfig::RejectInputPin,
                       PulseConfig::rejectDebounceMs() * 1000UL);
  CycleManager::begin(HardwareConfig::DefaultCycleTimeMs);
  IdleManager::begin(Config::lossAlarmDelaySeconds() * 1000UL);
  AlarmManager::begin(HardwareConfig::AlarmOutputPin, true);
  OEEManager::begin(HardwareConfig::DefaultCycleTimeMs);

  gState = MachineState::Ready;
  gReady = true;
  gHasProductionPulse = false;
  gLossClassifiedForCurrentIdle = false;
  EventBus::publish(EventType::MachineReady);
  Logger::info(String(F("[LOSS] Capture alarm activates after ")) +
               Config::lossAlarmDelaySeconds() + F(" idle seconds"));
  Logger::info(F("Machine engine ready"));
}

void MachineEngine::update() {
  const uint32_t nowMs = millis();

  while (ProductionManager::consumePulse()) {
    gHasProductionPulse = true;
    gLossClassifiedForCurrentIdle = false;
    CycleManager::onProduction(nowMs);
    IdleManager::onProduction();
    if (!AlarmManager::active()) gState = MachineState::Running;
    EventBus::publish(EventType::ProductionPulse, static_cast<int32_t>(ProductionManager::total()));
  }

  while (RejectManager::consumePulse()) {
    EventBus::publish(EventType::RejectPulse, static_cast<int32_t>(RejectManager::total()));
  }

  if (gHasProductionPulse) {
    IdleManager::update(
        CycleManager::cycleExpired(nowMs),
        nowMs,
        CycleManager::lastProductionMs(),
        CycleManager::cycleTimeMs());
  }

  const bool idleNow = gHasProductionPulse && IdleManager::idle();
  if (idleNow && !gWasIdle) {
    gLossStartIdleSeconds = IdleManager::idleSeconds();
    gLossClassifiedForCurrentIdle = false;
    gState = MachineState::Idle;
    EventBus::publish(EventType::IdleStarted);
  } else if (!idleNow && gWasIdle) {
    gLossClassifiedForCurrentIdle = false;
    EventBus::publish(EventType::IdleEnded);
  }
  gWasIdle = idleNow;

  if (gHasProductionPulse && IdleManager::alarmDue() && !gLossClassifiedForCurrentIdle) {
    if (!AlarmManager::active()) {
      AlarmManager::set(true);
      ProductionManager::setEnabled(false);
      Logger::warn(F("[LOSS] Production counting blocked until loss is acknowledged"));
    }
    gState = MachineState::LossRequired;
  }

  const bool alarmNow = AlarmManager::active();
  if (alarmNow && !gWasAlarmActive) EventBus::publish(EventType::AlarmActivated);
  if (!alarmNow && gWasAlarmActive) EventBus::publish(EventType::AlarmCleared);
  gWasAlarmActive = alarmNow;

  const bool oeeDowntime = !gHasProductionPulse || idleNow || alarmNow;
  OEEManager::update(oeeDowntime, ProductionManager::total(), RejectManager::total());
}

bool MachineEngine::ready() { return gReady; }

MachineSnapshot MachineEngine::snapshot() {
  const uint32_t total = ProductionManager::total();
  const uint32_t rejects = RejectManager::total();
  const OEESnapshot oee = OEEManager::snapshot();
  MachineSnapshot value;
  value.state = gState;
  value.totalParts = total;
  value.rejectParts = rejects;
  value.goodParts = total > rejects ? total - rejects : 0;
  value.lastProductionMs = CycleManager::lastProductionMs();
  value.idleSeconds = IdleManager::idleSeconds();
  value.runSeconds = oee.runSeconds;
  value.downtimeSeconds = oee.downtimeSeconds;
  value.targetQuantity = oee.targetQuantity;
  value.availabilityPermille = oee.availabilityPermille;
  value.performancePermille = oee.performancePermille;
  value.qualityPermille = oee.qualityPermille;
  value.oeePermille = oee.oeePermille;
  value.alarmActive = AlarmManager::active();
  return value;
}

bool MachineEngine::acknowledgeLossCode(uint16_t lossCode) {
  if (lossCode == 0 || lossCode > 16 || !AlarmManager::active() || gLossClassifiedForCurrentIdle) {
    return false;
  }

  const uint32_t idleNow = IdleManager::idleSeconds();
  const uint32_t duration = idleNow >= gLossStartIdleSeconds ? idleNow - gLossStartIdleSeconds : 0;
  OEEManager::recordLoss(lossCode, duration);
  EventBus::publish(EventType::LossSelected, lossCode, duration);

  gLossClassifiedForCurrentIdle = true;
  AlarmManager::clear();
  ProductionManager::setEnabled(true);
  gState = IdleManager::idle() ? MachineState::Idle : MachineState::Running;
  gLastAcceptedLossCode = lossCode;
  gLastLossDurationSeconds = duration;
  TelegramClient::queueLoss(lossCode, duration);
  Logger::info(F("[LOSS] Loss captured once; production counting re-enabled"));
  return true;
}

uint16_t MachineEngine::lastAcceptedLossCode() { return gLastAcceptedLossCode; }
uint32_t MachineEngine::lastLossDurationSeconds() { return gLastLossDurationSeconds; }
