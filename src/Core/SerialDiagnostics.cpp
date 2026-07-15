#include "SerialDiagnostics.h"
#include "Config.h"
#include "Logger.h"
#include "SystemHealth.h"
#include "../Communication/WiFiManager.h"
#include "../Communication/TimeManager.h"
#include "../Communication/CloudManager.h"
#include "../HMI/HMIManager.h"
#include "../Storage/OfflineQueue.h"
#include <ESP8266WiFi.h>

namespace {
bool gWifiKnown = false;
bool gLastWifi = false;
bool gTimeReported = false;
bool gHmiKnown = false;
bool gLastHmi = false;
bool gGoogleReported = false;
uint32_t gLastCloudSuccess = 0;
uint32_t gLastCloudFailure = 0;
uint16_t gLastQueueCount = 0;
uint32_t gLastHealthMs = 0;
constexpr uint32_t kHealthIntervalMs = 60000UL;

void divider() {
  Logger::info(F("================================================="));
}
}

void SerialDiagnostics::begin() {
  divider();
  Logger::info(F("AFMS serial diagnostics started"));
  Logger::info(String(F("[CFG] Machine ID: ")) + Config::machineId());
  Logger::info(String(F("[CFG] Machine Name: ")) + Config::machineName());
  Logger::info(String(F("[CFG] WiFi SSID: ")) + Config::wifiSsid());
  Logger::info(Config::googleWebAppUrl()[0]
                   ? F("[GOOGLE] Web App URL configured")
                   : F("[GOOGLE] Web App URL missing"));
  Logger::info(Config::apiToken()[0]
                   ? F("[GOOGLE] API token loaded")
                   : F("[GOOGLE] API token missing"));
  Logger::warn(F("[TELEGRAM] Module not yet implemented in this firmware"));
  Logger::info(String(F("[QUEUE] Pending records: ")) + OfflineQueue::count());
  divider();

  gLastQueueCount = OfflineQueue::count();
  gLastCloudSuccess = CloudManager::uploadSuccessCount();
  gLastCloudFailure = CloudManager::uploadFailureCount();
}

void SerialDiagnostics::update() {
  const bool wifi = WiFiManager::connected();
  if (!gWifiKnown || wifi != gLastWifi) {
    gWifiKnown = true;
    gLastWifi = wifi;
    if (wifi) {
      Logger::info(F("[WIFI] Connected"));
      Logger::info(String(F("[WIFI] IP: ")) + WiFi.localIP().toString());
      Logger::info(String(F("[WIFI] RSSI: ")) + WiFi.RSSI() + F(" dBm"));
    } else {
      Logger::warn(F("[WIFI] Disconnected"));
    }
  }

  if (!gTimeReported && TimeManager::synchronized()) {
    gTimeReported = true;
    Logger::info(String(F("[NTP] Time synchronized: ")) + TimeManager::iso8601());
  }

  const bool hmi = HMIManager::connected();
  if (!gHmiKnown || hmi != gLastHmi) {
    gHmiKnown = true;
    gLastHmi = hmi;
    if (hmi) Logger::info(F("[HMI] Modbus communication active"));
    else Logger::warn(F("[HMI] Waiting for Modbus communication"));
  }

  const uint32_t cloudSuccess = CloudManager::uploadSuccessCount();
  const uint32_t cloudFailure = CloudManager::uploadFailureCount();
  if (cloudSuccess != gLastCloudSuccess) {
    gLastCloudSuccess = cloudSuccess;
    Logger::info(String(F("[GOOGLE] Upload successful, total: ")) + cloudSuccess);
    if (!gGoogleReported) {
      gGoogleReported = true;
      Logger::info(F("[GOOGLE] Web App connection confirmed"));
    }
  }
  if (cloudFailure != gLastCloudFailure) {
    gLastCloudFailure = cloudFailure;
    Logger::warn(String(F("[GOOGLE] Upload failed, total: ")) + cloudFailure);
  }

  const uint16_t queueCount = OfflineQueue::count();
  if (queueCount != gLastQueueCount) {
    gLastQueueCount = queueCount;
    Logger::info(String(F("[QUEUE] Pending records: ")) + queueCount);
  }

  if (millis() - gLastHealthMs >= kHealthIntervalMs) {
    gLastHealthMs = millis();
    Logger::info(String(F("[HEALTH] Free heap: ")) + ESP.getFreeHeap() + F(" bytes"));
    Logger::info(String(F("[HEALTH] Uptime: ")) + (millis() / 1000UL) + F(" sec"));
  }
}
