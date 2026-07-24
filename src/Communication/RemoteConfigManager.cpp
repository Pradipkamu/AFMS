#include "RemoteConfigManager.h"
#include "../Core/Config.h"
#include "../Core/Logger.h"
#include <ArduinoJson.h>
#include <ESP8266HTTPClient.h>
#include <ESP8266WiFi.h>
#include <LittleFS.h>
#include <WiFiClient.h>

namespace {
constexpr const char *kMachinePath = "/machine.json";
constexpr const char *kTempPath = "/machine.remote.tmp";
constexpr const char *kBackupPath = "/machine.remote.bak";
constexpr uint32_t kDefaultCheckMs = 300000UL;
uint32_t gLastCheck = 0;
uint32_t gVersion = 0;
bool gUsingRemote = false;
char gError[80] = "not checked";

void setError(const String &value) { value.substring(0, sizeof(gError)-1).toCharArray(gError, sizeof(gError)); }

bool loadLocalSettings(String &serverUrl, String &apiKey, uint32_t &intervalMs) {
  File file = LittleFS.open(kMachinePath, "r");
  if (!file) return false;
  DynamicJsonDocument doc(8192);
  const auto error = deserializeJson(doc, file);
  file.close();
  if (error) return false;
  serverUrl = doc["afms_server_url"] | "";
  apiKey = doc["device_api_key"] | "";
  gVersion = doc["remote_config_version"] | 0;
  uint32_t seconds = doc["communication"]["configSync"]["checkIntervalSeconds"] | 300;
  seconds = constrain(seconds, 60UL, 86400UL);
  intervalMs = seconds * 1000UL;
  return serverUrl.length() > 0;
}

bool validateRemote(const JsonObjectConst config) {
  const JsonObjectConst web = config["communication"]["afmsWeb"];
  const JsonObjectConst google = config["communication"]["googleSheets"];
  const char *mode = web["mode"] | "HYBRID";
  if (strcmp(mode,"INTERVAL") && strcmp(mode,"CYCLE") && strcmp(mode,"HYBRID")) return false;
  const uint32_t interval = web["intervalSeconds"] | 60;
  const uint32_t heartbeat = web["heartbeatSeconds"] | 60;
  const uint32_t milestone = web["productionMilestone"] | 10;
  const uint32_t googleInterval = google["uploadIntervalSeconds"] | 3600;
  return interval >= 5 && interval <= 3600 && heartbeat >= 15 && heartbeat <= 3600 && milestone >= 1 && milestone <= 10000 && googleInterval >= 60 && googleInterval <= 86400;
}

bool applyRemote(const JsonObjectConst response) {
  const uint32_t version = response["configVersion"] | 0;
  const JsonObjectConst remote = response["configuration"];
  if (!version || remote.isNull() || !validateRemote(remote)) { setError("remote config rejected"); return false; }

  DynamicJsonDocument local(12288);
  File current = LittleFS.open(kMachinePath, "r");
  if (current) { deserializeJson(local, current); current.close(); }
  local["remote_config_version"] = version;
  local["remote_config_updated_at"] = response["updatedAt"] | "";
  local["communication"] = remote["communication"];

  File temp = LittleFS.open(kTempPath, "w");
  if (!temp || serializeJsonPretty(local, temp) == 0) { if (temp) temp.close(); LittleFS.remove(kTempPath); setError("unable to save config"); return false; }
  temp.flush(); temp.close();
  LittleFS.remove(kBackupPath);
  if (LittleFS.exists(kMachinePath) && !LittleFS.rename(kMachinePath, kBackupPath)) { LittleFS.remove(kTempPath); setError("backup failed"); return false; }
  if (!LittleFS.rename(kTempPath, kMachinePath)) { if (LittleFS.exists(kBackupPath)) LittleFS.rename(kBackupPath, kMachinePath); setError("activation failed"); return false; }
  LittleFS.remove(kBackupPath);
  gVersion = version;
  gUsingRemote = Config::load();
  setError(gUsingRemote ? "none" : "saved but reload failed");
  return gUsingRemote;
}

void checkNow() {
  if (WiFi.status() != WL_CONNECTED) { setError("offline; using local config"); return; }
  String serverUrl, apiKey; uint32_t intervalMs = kDefaultCheckMs;
  if (!loadLocalSettings(serverUrl, apiKey, intervalMs)) { setError("server URL not configured"); return; }
  WiFiClient client; HTTPClient http;
  String url = serverUrl + "/api/v1/devices/" + Config::machineId() + "/config?currentVersion=" + String(gVersion);
  if (!http.begin(client, url)) { setError("HTTP begin failed"); return; }
  if (apiKey.length()) http.addHeader("X-AFMS-Device-Key", apiKey);
  const int code = http.GET();
  if (code == HTTP_CODE_NOT_MODIFIED) { setError("none"); http.end(); return; }
  if (code != HTTP_CODE_OK) { setError(String("config HTTP ") + code); http.end(); return; }
  DynamicJsonDocument response(12288);
  const auto error = deserializeJson(response, http.getStream());
  http.end();
  if (error) { setError("invalid config JSON"); return; }
  applyRemote(response.as<JsonObjectConst>());
}
}

void RemoteConfigManager::begin() { gLastCheck = millis() - kDefaultCheckMs; Logger::info(F("[CONFIG] Remote config manager ready")); }
void RemoteConfigManager::update() { String url,key; uint32_t intervalMs=kDefaultCheckMs; loadLocalSettings(url,key,intervalMs); if (millis()-gLastCheck>=intervalMs) { gLastCheck=millis(); checkNow(); } }
uint32_t RemoteConfigManager::activeVersion() { return gVersion; }
bool RemoteConfigManager::usingRemoteConfig() { return gUsingRemote; }
const char *RemoteConfigManager::lastError() { return gError; }
