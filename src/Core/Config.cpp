#include "Config.h"
#include "Logger.h"
#include <LittleFS.h>

namespace {
char gMachineId[16] = "MCH001";
char gMachineName[32] = "PRESS-01";
char gWifiSsid[33] = "";
char gWifiPassword[65] = "";
char gGoogleWebAppUrl[220] = "";

void copyJsonString(const String &json, const char *key, char *dest, size_t size) {
  const String token = String('"') + key + "\"";
  int keyPos = json.indexOf(token);
  if (keyPos < 0) return;
  int colon = json.indexOf(':', keyPos + token.length());
  int firstQuote = json.indexOf('"', colon + 1);
  int secondQuote = json.indexOf('"', firstQuote + 1);
  if (colon < 0 || firstQuote < 0 || secondQuote < 0) return;
  String value = json.substring(firstQuote + 1, secondQuote);
  value.replace("\\\"", "\"");
  value.replace("\\\\", "\\");
  value.toCharArray(dest, size);
}

String escapeJson(const char *value) {
  String out;
  if (!value) return out;
  while (*value) {
    if (*value == '"' || *value == '\\') out += '\\';
    out += *value++;
  }
  return out;
}

void assign(const String &value, char *dest, size_t size) {
  value.substring(0, size - 1).toCharArray(dest, size);
}
}

bool Config::load() {
  File file = LittleFS.open("/machine.json", "r");
  if (!file) {
    Logger::warn(F("Config file not found; using defaults"));
    return false;
  }

  const String json = file.readString();
  file.close();

  copyJsonString(json, "machine_id", gMachineId, sizeof(gMachineId));
  copyJsonString(json, "machine_name", gMachineName, sizeof(gMachineName));
  copyJsonString(json, "wifi_ssid", gWifiSsid, sizeof(gWifiSsid));
  copyJsonString(json, "wifi_password", gWifiPassword, sizeof(gWifiPassword));
  copyJsonString(json, "google_web_app_url", gGoogleWebAppUrl, sizeof(gGoogleWebAppUrl));

  Logger::info(String(F("Config loaded for ")) + gMachineId);
  return true;
}

bool Config::save() {
  File backup = LittleFS.open("/machine.json", "r");
  if (backup) {
    File backupOut = LittleFS.open("/machine.json.bak", "w");
    while (backup.available()) backupOut.write(backup.read());
    backup.close();
    backupOut.close();
  }

  File file = LittleFS.open("/machine.json", "w");
  if (!file) return false;
  file.print(F("{\n  \"machine_id\": \"")); file.print(escapeJson(gMachineId));
  file.print(F("\",\n  \"machine_name\": \"")); file.print(escapeJson(gMachineName));
  file.print(F("\",\n  \"wifi_ssid\": \"")); file.print(escapeJson(gWifiSsid));
  file.print(F("\",\n  \"wifi_password\": \"")); file.print(escapeJson(gWifiPassword));
  file.print(F("\",\n  \"google_web_app_url\": \"")); file.print(escapeJson(gGoogleWebAppUrl));
  file.print(F("\"\n}\n"));
  file.close();
  Logger::info(F("Configuration saved"));
  return true;
}

bool Config::update(const String &machineId,
                    const String &machineName,
                    const String &wifiSsid,
                    const String &wifiPassword,
                    const String &googleWebAppUrl) {
  if (!machineId.length() || !machineName.length()) return false;
  assign(machineId, gMachineId, sizeof(gMachineId));
  assign(machineName, gMachineName, sizeof(gMachineName));
  assign(wifiSsid, gWifiSsid, sizeof(gWifiSsid));
  assign(wifiPassword, gWifiPassword, sizeof(gWifiPassword));
  assign(googleWebAppUrl, gGoogleWebAppUrl, sizeof(gGoogleWebAppUrl));
  return save();
}

const char *Config::machineId() { return gMachineId; }
const char *Config::machineName() { return gMachineName; }
const char *Config::wifiSsid() { return gWifiSsid; }
const char *Config::wifiPassword() { return gWifiPassword; }
const char *Config::googleWebAppUrl() { return gGoogleWebAppUrl; }
