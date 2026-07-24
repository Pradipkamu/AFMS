#include "ReconnectManager.h"
#include "WiFiManager.h"
#include "CloudManager.h"
#include "TelegramClient.h"
#include "../Core/Logger.h"

namespace {
bool gLastWifiConnected = false;
bool gGoogleCheckRequested = false;
uint32_t gLastGoogleCheckMs = 0;
constexpr uint32_t kGoogleConnectivityCheckMs = 300000UL;

void requestCloudChecks(const __FlashStringHelper *reason) {
  TelegramClient::begin();
  CloudManager::queueStatusNow();
  gGoogleCheckRequested = true;
  gLastGoogleCheckMs = millis();
  Logger::info(String(F("[CONNECTIVITY] ")) + reason +
               F("; Google status and Telegram checks requested"));
}
}

void ReconnectManager::begin() {
  gLastWifiConnected = WiFiManager::connected();
  gGoogleCheckRequested = false;
  gLastGoogleCheckMs = 0;

  // If Wi-Fi was already established before this manager started, still run
  // the cloud checks instead of waiting for a later disconnect/reconnect.
  if (gLastWifiConnected) requestCloudChecks(F("WiFi already connected"));
}

void ReconnectManager::update() {
  const bool wifiConnected = WiFiManager::connected();

  if (!wifiConnected) {
    if (gLastWifiConnected) {
      TelegramClient::begin();
      Logger::warn(F("[CONNECTIVITY] WiFi lost; cloud indicators cleared"));
    }
    gLastWifiConnected = false;
    gGoogleCheckRequested = false;
    return;
  }

  if (!gLastWifiConnected) {
    gLastWifiConnected = true;
    requestCloudChecks(F("WiFi restored"));
    return;
  }

  // Keep Google reachability independent from production/loss traffic. A
  // lightweight status record is queued every five minutes while Wi-Fi is up,
  // allowing the HMI Google indicator to recover without waiting for an event.
  const uint32_t now = millis();
  if (!gGoogleCheckRequested || now - gLastGoogleCheckMs >= kGoogleConnectivityCheckMs) {
    requestCloudChecks(F("Periodic reachability check"));
  }
}
