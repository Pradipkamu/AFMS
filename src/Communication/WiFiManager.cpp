#include "WiFiManager.h"
#include "../Core/EventBus.h"
#include "../Core/Logger.h"
#include <ESP8266WiFi.h>

namespace {
char gSsid[33] = "";
char gPassword[65] = "";
uint32_t gLastAttemptMs = 0;
uint32_t gReconnectCount = 0;
bool gWasConnected = false;
constexpr uint32_t kRetryIntervalMs = 10000;

void connectNow() {
  if (gSsid[0] == '\0') {
    return;
  }
  WiFi.mode(WIFI_STA);
  WiFi.begin(gSsid, gPassword);
  gLastAttemptMs = millis();
  ++gReconnectCount;
  Logger::info(F("WiFi connection attempt"));
}
}

void WiFiManager::begin(const char *ssid, const char *password) {
  strlcpy(gSsid, ssid ? ssid : "", sizeof(gSsid));
  strlcpy(gPassword, password ? password : "", sizeof(gPassword));
  WiFi.persistent(false);
  WiFi.setAutoReconnect(true);
  connectNow();
}

void WiFiManager::update() {
  const bool isConnected = WiFi.status() == WL_CONNECTED;

  if (isConnected != gWasConnected) {
    gWasConnected = isConnected;
    if (isConnected) {
      EventBus::publish(EventType::WifiConnected, WiFi.RSSI());
      Logger::info(String(F("WiFi connected: ")) + WiFi.localIP().toString());
    } else {
      EventBus::publish(EventType::WifiDisconnected);
      Logger::warn(F("WiFi disconnected"));
    }
  }

  if (!isConnected && millis() - gLastAttemptMs >= kRetryIntervalMs) {
    WiFi.disconnect();
    connectNow();
  }
}

bool WiFiManager::connected() { return WiFi.status() == WL_CONNECTED; }
int32_t WiFiManager::rssi() { return connected() ? WiFi.RSSI() : -127; }
uint32_t WiFiManager::reconnectCount() { return gReconnectCount; }
