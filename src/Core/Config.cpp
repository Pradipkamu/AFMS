#include "Config.h"
#include "Logger.h"
#include <LittleFS.h>
#include <cstring>

namespace {
char gMachineId[16] = "MCH001";
char gMachineName[32] = "PRESS-01";
char gWifiSsid[33] = "";
char gWifiPassword[65] = "";
char gGoogleWebAppUrl[220] = "";
char gApiToken[96] = "";
char gShiftName[3][9] = {"A", "B", "C"};
char gShiftStart[3][6] = {"06:00", "14:00", "22:00"};
char gShiftEnd[3][6] = {"14:00", "22:00", "06:00"};
uint16_t gShiftStartMinutes[3] = {360, 840, 1320};
uint16_t gShiftEndMinutes[3] = {840, 1320, 360};
bool gShiftScheduleValid = false;

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

bool parseTime(const char *value, uint16_t &minutes) {
  if (!value || strlen(value) != 5 || value[2] != ':') return false;
  if (!isDigit(value[0]) || !isDigit(value[1]) || !isDigit(value[3]) || !isDigit(value[4])) return false;
  const uint8_t hour = static_cast<uint8_t>((value[0] - '0') * 10 + (value[1] - '0'));
  const uint8_t minute = static_cast<uint8_t>((value[3] - '0') * 10 + (value[4] - '0'));
  if (hour > 23 || minute > 59) return false;
  minutes = static_cast<uint16_t>(hour * 60U + minute);
  return true;
}

bool validateShiftSchedule() {
  for (uint8_t i = 0; i < 3; ++i) {
    if (!gShiftName[i][0]) return false;
    if (!parseTime(gShiftStart[i], gShiftStartMinutes[i])) return false;
    if (!parseTime(gShiftEnd[i], gShiftEndMinutes[i])) return false;
    if (gShiftStartMinutes[i] == gShiftEndMinutes[i]) return false;
  }
  if (strcmp(gShiftName[0], gShiftName[1]) == 0 ||
      strcmp(gShiftName[0], gShiftName[2]) == 0 ||
      strcmp(gShiftName[1], gShiftName[2]) == 0) return false;
  return gShiftEndMinutes[0] == gShiftStartMinutes[1] &&
         gShiftEndMinutes[1] == gShiftStartMinutes[2] &&
         gShiftEndMinutes[2] == gShiftStartMinutes[0];
}
}

bool Config::load() {
  File file = LittleFS.open("/machine.json", "r");
  if (!file) {
    Logger::warn(F("Config file not found; using defaults"));
    gShiftScheduleValid = false;
    return false;
  }

  const String json = file.readString();
  file.close();

  copyJsonString(json, "machine_id", gMachineId, sizeof(gMachineId));
  copyJsonString(json, "machine_name", gMachineName, sizeof(gMachineName));
  copyJsonString(json, "wifi_ssid", gWifiSsid, sizeof(gWifiSsid));
  copyJsonString(json, "wifi_password", gWifiPassword, sizeof(gWifiPassword));
  copyJsonString(json, "google_web_app_url", gGoogleWebAppUrl, sizeof(gGoogleWebAppUrl));
  copyJsonString(json, "api_token", gApiToken, sizeof(gApiToken));
  copyJsonString(json, "shift_1_name", gShiftName[0], sizeof(gShiftName[0]));
  copyJsonString(json, "shift_1_start", gShiftStart[0], sizeof(gShiftStart[0]));
  copyJsonString(json, "shift_1_end", gShiftEnd[0], sizeof(gShiftEnd[0]));
  copyJsonString(json, "shift_2_name", gShiftName[1], sizeof(gShiftName[1]));
  copyJsonString(json, "shift_2_start", gShiftStart[1], sizeof(gShiftStart[1]));
  copyJsonString(json, "shift_2_end", gShiftEnd[1], sizeof(gShiftEnd[1]));
  copyJsonString(json, "shift_3_name", gShiftName[2], sizeof(gShiftName[2]));
  copyJsonString(json, "shift_3_start", gShiftStart[2], sizeof(gShiftStart[2]));
  copyJsonString(json, "shift_3_end", gShiftEnd[2], sizeof(gShiftEnd[2]));

  gShiftScheduleValid = validateShiftSchedule();
  Logger::info(String(F("Config loaded for ")) + gMachineId);
  if (!gShiftScheduleValid) Logger::error(F("[SHIFT] Invalid shift configuration; automatic shifts disabled"));
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
  file.print(F("\",\n  \"api_token\": \"")); file.print(escapeJson(gApiToken));
  for (uint8_t i = 0; i < 3; ++i) {
    file.print(F("\",\n  \"shift_")); file.print(i + 1); file.print(F("_name\": \"")); file.print(escapeJson(gShiftName[i]));
    file.print(F("\",\n  \"shift_")); file.print(i + 1); file.print(F("_start\": \"")); file.print(gShiftStart[i]);
    file.print(F("\",\n  \"shift_")); file.print(i + 1); file.print(F("_end\": \"")); file.print(gShiftEnd[i]);
  }
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
const char *Config::apiToken() { return gApiToken; }
const char *Config::shiftName(uint8_t index) { return index < 3 ? gShiftName[index] : ""; }
const char *Config::shiftStart(uint8_t index) { return index < 3 ? gShiftStart[index] : ""; }
const char *Config::shiftEnd(uint8_t index) { return index < 3 ? gShiftEnd[index] : ""; }
uint16_t Config::shiftStartMinutes(uint8_t index) { return index < 3 ? gShiftStartMinutes[index] : 0; }
uint16_t Config::shiftEndMinutes(uint8_t index) { return index < 3 ? gShiftEndMinutes[index] : 0; }
bool Config::shiftScheduleValid() { return gShiftScheduleValid; }