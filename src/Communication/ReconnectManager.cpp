#include "ReconnectManager.h"
#include "WiFiManager.h"
#include "CloudManager.h"
#include "TelegramClient.h"
#include "../Core/Logger.h"

namespace {
bool gLastWifiConnected = false;
}

void ReconnectManager::begin() {
  gLastWifiConnected = WiFiManager::connected();
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

  // Force Telegram getMe verification on the next CloudManager update.
  TelegramClient::begin();

  // Queue an immediate Google status payload so reachability is checked now,
  // instead of waiting for the next hourly/event upload cycle.
  CloudManager::queueStatusNow();
  Logger::info(F("[CONNECTIVITY] WiFi restored; immediate Google and Telegram checks requested"));
}
