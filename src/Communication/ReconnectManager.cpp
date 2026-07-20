#include "ReconnectManager.h"
#include "WiFiManager.h"
#include "CloudManager.h"
#include "TelegramClient.h"
#include "../Core/Logger.h"

namespace {
bool gLastWifiConnected = false;

void requestCloudChecks(const __FlashStringHelper *reason) {
  // Force Telegram getMe verification on the next CloudManager update.
  TelegramClient::begin();

  // Queue an immediate lightweight Google status payload so reachability is
  // verified without waiting for the next hourly, shift, or loss report.
  CloudManager::queueStatusNow();
  Logger::info(reason);
}
}

void ReconnectManager::begin() {
  gLastWifiConnected = WiFiManager::connected();

  // WiFi may already be connected before this manager starts. In that case no
  // disconnected-to-connected edge will occur, so request the boot checks now.
  if (gLastWifiConnected) {
    requestCloudChecks(F("[CONNECTIVITY] Boot WiFi ready; immediate Google and Telegram checks requested"));
  }
}

void ReconnectManager::update() {
  const bool wifiConnected = WiFiManager::connected();
  if (wifiConnected == gLastWifiConnected) return;

  gLastWifiConnected = wifiConnected;

  if (!wifiConnected) {
    // Clear Telegram verification immediately so the HMI indicator goes OFF.
    TelegramClient::begin();
    Logger::warn(F("[CONNECTIVITY] WiFi lost; cloud indicators cleared"));
    return;
  }

  requestCloudChecks(F("[CONNECTIVITY] WiFi restored; immediate Google and Telegram checks requested"));
}
