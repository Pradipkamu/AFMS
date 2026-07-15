#include "SerialDiagnostics.h"
#include "Config.h"
#include "Logger.h"
#include "SystemHealth.h"
#include "../Communication/WiFiManager.h"
#include "../Communication/TimeManager.h"
#include "../Communication/CloudManager.h"
#include "../Communication/TelegramClient.h"
#include "../HMI/HMIManager.h"
#include "../Storage/OfflineQueue.h"
#include <ESP8266WiFi.h>
#include <LittleFS.h>

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

void divider() { Logger::info(F("=================================================")); }

void configStatus(const __FlashStringHelper *name, const char *value) {
  Logger::info(String(F("[CHECK] ")) + String(name) + F(" ........ ") +
               ((value && value[0]) ? F("OK") : F("MISSING")));
}

void fileStatus(const char *path, bool required) {
  const bool exists = LittleFS.exists(path);
  const String prefix = required ? F("[CHECK] Required file ") : F("[CHECK] Runtime file ");
  Logger::info(prefix + path + F(" ........ ") + (exists ? F("PRESENT") : F("NOT FOUND")));
}

void printShiftConfig() {
  Logger::info(F("[SHIFT] Configured schedule"));
  for (uint8_t i = 0; i < 3; ++i) {
    Logger::info(String(F("[SHIFT] ")) + Config::shiftName(i) + F(": ") +
                 Config::shiftStart(i) + F(" - ") + Config::shiftEnd(i));
  }
  Logger::info(Config::shiftScheduleValid()
                   ? F("[SHIFT] Schedule validation: OK")
                   : F("[SHIFT] Schedule validation: INVALID"));
}
}

void SerialDiagnostics::begin() {
  divider();
  Logger::info(F("AFMS serial diagnostics started"));
  Logger::info(String(F("[CFG] Machine ID: ")) + Config::machineId());
  Logger::info(String(F("[CFG] Machine Name: ")) + Config::machineName());
  Logger::info(String(F("[CFG] WiFi SSID: ")) + Config::wifiSsid());

  Logger::info(F("[CHECK] Startup configuration"));
  configStatus(F("Machine ID"), Config::machineId());
  configStatus(F("Machine Name"), Config::machineName());
  configStatus(F("WiFi SSID"), Config::wifiSsid());
  configStatus(F("WiFi Password"), Config::wifiPassword());
  configStatus(F("Google Web App URL"), Config::googleWebAppUrl());
  configStatus(F("Google API Token"), Config::apiToken());
  printShiftConfig();

  Logger::info(F("[CHECK] LittleFS files"));
  fileStatus("/machine.json", true);
  fileStatus("/machine.json.bak", false);
  fileStatus("/queue.dat", false);
  fileStatus("/fault.log", false);

  Logger::info(Config::googleWebAppUrl()[0]
                   ? F("[GOOGLE] Web App URL configured")
                   : F("[GOOGLE] Web App URL missing"));
  Logger::info(Config::apiToken()[0]
                   ? F("[GOOGLE] API token loaded")
                   : F("[GOOGLE] API token missing"));
  Logger::info(TelegramClient::configured()
                   ? F("[TELEGRAM] Configuration loaded")
                   : F("[TELEGRAM] Bot token or chat ID missing"));
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