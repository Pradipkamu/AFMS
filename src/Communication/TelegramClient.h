#pragma once
#include <Arduino.h>

namespace TelegramClient {
void begin();
void update();
bool configured();
bool connected();
bool sendMachineReady();
bool sendLoss(uint16_t lossCode, uint32_t durationSeconds);
uint32_t successCount();
uint32_t failureCount();
}
