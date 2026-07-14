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

namespace {
bool gReady = false;
MachineState gState = MachineState::Ready;
bool gWasIdle = false;
bool gWasAlarmActive = false;
uint32_t gLossStartIdleSeconds = 0;
}

void MachineEngine::begin() {
  ProductionManager::begin(HardwareConfig::ProductionInputPin, HardwareConfig::InputDebounceUs);
  RejectManager::begin(HardwareConfig::RejectInputPin, HardwareConfig::InputDebounceUs);
  CycleManager::begin(HardwareConfig::DefaultCycleTimeMs);
  IdleManager::begin(HardwareConfig::DefaultIdleDelayMs);
  AlarmManager::begin(HardwareConfig::AlarmOutputPin, true);
  OEEManager::begin(HardwareConfig::DefaultCycleTimeMs);

  gState = MachineState::Ready;
  gReady = true;
  EventBus::publish(EventType::MachineReady);
  Logger::info(F("Machine engine ready"));
}

void MachineEngine::update() {
  const uint32_t nowMs = millis();

  while (ProductionManager::consumePulse()) {
    CycleManager::onProduction(nowMs);
    IdleManager::onProduction();
    if (!AlarmManager::active()) gState = MachineState::Running;
    EventBus::publish(EventType::ProductionPulse, static_cast<int32_t>(ProductionManager::total()));
  }

  while (RejectManager::consumePulse()) {
    EventBus::publish(EventType::RejectPulse, static_cast<int32_t>(RejectManager::total()));
  }

  IdleManager::update(
      CycleManager::cycleExpired(nowMs),
      nowMs,
      CycleManager::lastProductionMs(),
      CycleManager::cycleTimeMs());

  const bool idleNow = IdleManager::idle();
  if (idleNow && !gWasIdle) {
    gState = MachineState::Idle;
    EventBus::publish(EventType::IdleStarted);
  } else if (!idleNow && gWasIdle) {
    EventBus::publish(EventType::IdleEnded);
  }
  gWasIdle = idleNow;

  if (IdleManager::alarmDue()) {
    AlarmManager::set(true);
    gState = MachineState::LossRequired;
  }

  const bool alarmNow = AlarmManager::active();
  if (alarmNow && !gWasAlarmActive) {
    gLossStartIdleSeconds = IdleManager::idleSeconds();
    EventBus::publish(EventType::AlarmActivated);
  }
  if (!alarmNow && gWasAlarmActive) EventBus::publish(EventType::AlarmCleared);
  gWasAlarmActive = alarmNow;

  OEEManager::update(idleNow || alarmNow, ProductionManager::total(), RejectManager::total());
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

void MachineEngine::acknowledgeLossCode(uint16_t lossCode) {
  if (lossCode == 0 || lossCode > 16 || !AlarmManager::active()) return;
  const uint32_t idleNow = IdleManager::idleSeconds();
  const uint32_t duration = idleNow >= gLossStartIdleSeconds ? idleNow - gLossStartIdleSeconds : 0;
  OEEManager::recordLoss(lossCode, duration);
  EventBus::publish(EventType::LossSelected, lossCode);
  AlarmManager::clear();
  gState = IdleManager::idle() ? MachineState::Idle : MachineState::Running;
}
