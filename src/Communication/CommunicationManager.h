#pragma once
#include <Arduino.h>

namespace CommunicationManager {
enum class Trigger : uint8_t { Periodic, Heartbeat, ProductionMilestone, StatusChange, LossChange, ShiftChange };
void begin();
void update();
void notify(Trigger trigger);
bool webDue();
bool googleDue();
void markWebComplete(bool success);
void markGoogleComplete(bool success);
uint32_t webSuccessCount();
uint32_t webFailureCount();
uint32_t googleSuccessCount();
uint32_t googleFailureCount();
}
