#pragma once

#include <Arduino.h>

namespace LossReportManager {

// Persists one Google loss-event record and one Telegram loss notification.
// This function performs only local string building and LittleFS outbox writes;
// network delivery remains asynchronous in the normal cloud update loop.
bool queue(uint16_t lossCode, uint32_t durationSeconds);

}  // namespace LossReportManager
