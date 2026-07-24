#pragma once
#include <Arduino.h>
namespace CommunicationManager {
enum class Trigger : uint8_t { Periodic, Heartbeat, ProductionMilestone, StatusChange, LossChange, ShiftChange };
void begin();
void update();
void reloadConfiguration();
void notify(Trigger trigger);
bool webEnabled();
bool googleEnabled();
bool webDue();
bool googleDue();
uint32_t webIntervalSeconds();
uint32_t googleIntervalSeconds();
uint32_t heartbeatSeconds();
uint32_t productionMilestone();
const char *webMode();
void markWebComplete(bool success);
void markGoogleComplete(bool success);
uint32_t webSuccessCount();
uint32_t webFailureCount();
uint32_t googleSuccessCount();
uint32_t googleFailureCount();
}
