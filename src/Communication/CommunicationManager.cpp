#include "CommunicationManager.h"
#include "../Core/Logger.h"

namespace {
constexpr uint32_t kWebIntervalMs = 60000UL;
constexpr uint32_t kHeartbeatMs = 60000UL;
constexpr uint32_t kGoogleIntervalMs = 3600000UL;
uint32_t gLastWeb = 0, gLastHeartbeat = 0, gLastGoogle = 0;
bool gWebDue = false, gGoogleDue = false;
uint32_t gWebOk = 0, gWebFail = 0, gGoogleOk = 0, gGoogleFail = 0;
}

void CommunicationManager::begin() {
  const uint32_t now = millis();
  gLastWeb = gLastHeartbeat = gLastGoogle = now;
  Logger::info(F("[COMM] Communication manager ready"));
}

void CommunicationManager::update() {
  const uint32_t now = millis();
  if (now - gLastWeb >= kWebIntervalMs) { gWebDue = true; gLastWeb = now; }
  if (now - gLastHeartbeat >= kHeartbeatMs) { gWebDue = true; gLastHeartbeat = now; }
  if (now - gLastGoogle >= kGoogleIntervalMs) { gGoogleDue = true; gLastGoogle = now; }
}

void CommunicationManager::notify(Trigger trigger) {
  switch (trigger) {
    case Trigger::StatusChange:
    case Trigger::LossChange:
    case Trigger::ShiftChange:
    case Trigger::ProductionMilestone:
    case Trigger::Heartbeat:
      gWebDue = true;
      break;
    case Trigger::Periodic:
      gWebDue = true;
      gGoogleDue = true;
      break;
  }
}

bool CommunicationManager::webDue() { return gWebDue; }
bool CommunicationManager::googleDue() { return gGoogleDue; }
void CommunicationManager::markWebComplete(bool success) { gWebDue = false; success ? ++gWebOk : ++gWebFail; }
void CommunicationManager::markGoogleComplete(bool success) { gGoogleDue = false; success ? ++gGoogleOk : ++gGoogleFail; }
uint32_t CommunicationManager::webSuccessCount() { return gWebOk; }
uint32_t CommunicationManager::webFailureCount() { return gWebFail; }
uint32_t CommunicationManager::googleSuccessCount() { return gGoogleOk; }
uint32_t CommunicationManager::googleFailureCount() { return gGoogleFail; }
