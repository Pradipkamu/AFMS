#include "Config.h"
#include "Logger.h"
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <cstring>

namespace {
constexpr const char *kConfigPath = "/machine.json";
constexpr const char *kTempPath = "/machine.json.tmp";
constexpr const char *kBackupPath = "/machine.json.bak";
constexpr uint32_t kDefaultLossAlarmDelaySeconds = 600UL;
constexpr uint32_t kMinLossAlarmDelaySeconds = 1UL;
constexpr uint32_t kMaxLossAlarmDelaySeconds = 86400UL;
constexpr uint16_t kDefaultHourlyUploadDelaySeconds = 0U;
constexpr uint16_t kMaxHourlyUploadDelaySeconds = 1200U;

char gMachineId[16] = "MCH001";
char gMachineName[32] = "PRESS-01";
char gWifiSsid[33] = "";
char gWifiPassword[65] = "";
char gGoogleWebAppUrl[220] = "";
char gApiToken[96] = "";
char gShiftName[3][9] = {"A", "B", "C"};
char gShiftStart[3][6] = {"08:00", "16:00", "00:00"};
char gShiftEnd[3][6] = {"16:00", "00:00", "08:00"};
uint16_t gShiftStartMinutes[3] = {480, 960, 0};
uint16_t gShiftEndMinutes[3] = {960, 0, 480};
bool gShiftScheduleValid = false;
uint32_t gLossAlarmDelaySeconds = kDefaultLossAlarmDelaySeconds;
uint16_t gHourlyUploadDelaySeconds = kDefaultHourlyUploadDelaySeconds;
bool gAlarmActiveHigh = true;

void copyValue(const char *value, char *dest, size_t size) {
  strlcpy(dest, value ? value : "", size);
}

void assign(const String &value, char *dest, size_t size) {
  value.substring(0, size - 1).toCharArray(dest, size);
}

bool parseTime(const char *value, uint16_t &minutes) {
  if (!value || strlen(value) != 5 || value[2] != ':') return false;
  if (!isDigit(value[0]) || !isDigit(value[1]) || !isDigit(value[3]) || !isDigit(value[4])) return false;
  const uint8_t hour = static_cast<uint8_t>((value[0] - '0') * 10 + (value[1] - '0'));
  const uint8_t minute = static_cast<uint8_t>((value[3] - '0') * 10 + (value[4] - '0'));
  if (minute > 59) return false;
  if (hour == 24) {
    if (minute != 0) return false;
    minutes = 0;
    return true;
  }
  if (hour > 23) return false;
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

bool loadDocument(DynamicJsonDocument &document) {
  File file = LittleFS.open(kConfigPath, "r");
  if (!file) return false;
  const DeserializationError error = deserializeJson(document, file);
  file.close();
  return !error;
}

uint32_t readLossAlarmDelaySeconds(const JsonDocument &document) {
  uint32_t value = kDefaultLossAlarmDelaySeconds;
  if (document.containsKey("loss_alarm_delay_seconds")) {
    value = document["loss_alarm_delay_seconds"].as<uint32_t>();
  } else if (document.containsKey("idle_delay_seconds")) {
    value = document["idle_delay_seconds"].as<uint32_t>();
    Logger::warn(F("[LOSS] Legacy idle_delay_seconds detected; use loss_alarm_delay_seconds"));
  } else if (document.containsKey("idleDelaySeconds")) {
    value = document["idleDelaySeconds"].as<uint32_t>();
    Logger::warn(F("[LOSS] Legacy idleDelaySeconds detected; use loss_alarm_delay_seconds"));
  }
  if (value < kMinLossAlarmDelaySeconds || value > kMaxLossAlarmDelaySeconds) {
    Logger::warn(F("[LOSS] Alarm delay out of range; using 600 seconds"));
    return kDefaultLossAlarmDelaySeconds;
  }
  return value;
}

uint16_t readHourlyUploadDelaySeconds(const JsonDocument &document) {
  const uint32_t value = document["hourly_upload_delay_seconds"] | kDefaultHourlyUploadDelaySeconds;
  if (value > kMaxHourlyUploadDelaySeconds) {
    Logger::warn(F("[GOOGLE] Hourly upload delay out of range; using 0 seconds"));
    return kDefaultHourlyUploadDelaySeconds;
  }
  return static_cast<uint16_t>(value);
}
}

bool Config::load() {
  DynamicJsonDocument document(6144);
  if (!loadDocument(document)) {
    Logger::warn(F("Config file missing or invalid; using defaults"));
    gShiftScheduleValid = false;
    gLossAlarmDelaySeconds = kDefaultLossAlarmDelaySeconds;
    gHourlyUploadDelaySeconds = kDefaultHourlyUploadDelaySeconds;
    gAlarmActiveHigh = true;
    return false;
  }

  copyValue(document["machine_id"] | gMachineId, gMachineId, sizeof(gMachineId));
  copyValue(document["machine_name"] | gMachineName, gMachineName, sizeof(gMachineName));
  copyValue(document["wifi_ssid"] | gWifiSsid, gWifiSsid, sizeof(gWifiSsid));
  copyValue(document["wifi_password"] | gWifiPassword, gWifiPassword, sizeof(gWifiPassword));
  copyValue(document["google_web_app_url"] | gGoogleWebAppUrl, gGoogleWebAppUrl, sizeof(gGoogleWebAppUrl));
  copyValue(document["api_token"] | gApiToken, gApiToken, sizeof(gApiToken));
  for (uint8_t i = 0; i < 3; ++i) {
    String prefix = String(F("shift_")) + (i + 1);
    copyValue(document[(prefix + F("_name")).c_str()] | gShiftName[i], gShiftName[i], sizeof(gShiftName[i]));
    copyValue(document[(prefix + F("_start")).c_str()] | gShiftStart[i], gShiftStart[i], sizeof(gShiftStart[i]));
    copyValue(document[(prefix + F("_end")).c_str()] | gShiftEnd[i], gShiftEnd[i], sizeof(gShiftEnd[i]));
  }

  gLossAlarmDelaySeconds = readLossAlarmDelaySeconds(document);
  gHourlyUploadDelaySeconds = readHourlyUploadDelaySeconds(document);
  gAlarmActiveHigh = document["alarm_active_high"] | true;
  gShiftScheduleValid = validateShiftSchedule();
  Logger::info(String(F("Config loaded for ")) + gMachineId);
  Logger::info(String(F("[LOSS] Alarm delay: ")) + gLossAlarmDelaySeconds + F(" sec"));
  Logger::info(String(F("[GOOGLE] Hourly upload delay: ")) + gHourlyUploadDelaySeconds + F(" sec"));
  Logger::info(String(F("[LOSS] Alarm output active ")) + (gAlarmActiveHigh ? F("HIGH") : F("LOW")));
  if (!gShiftScheduleValid) Logger::error(F("[SHIFT] Invalid shift configuration; automatic shifts disabled"));
  return true;
}

bool Config::save() {
  DynamicJsonDocument document(6144);
  loadDocument(document);

  document["machine_id"] = gMachineId;
  document["machine_name"] = gMachineName;
  document["wifi_ssid"] = gWifiSsid;
  document["wifi_password"] = gWifiPassword;
  document["google_web_app_url"] = gGoogleWebAppUrl;
  document["api_token"] = gApiToken;
  document["loss_alarm_delay_seconds"] = gLossAlarmDelaySeconds;
  document["hourly_upload_delay_seconds"] = gHourlyUploadDelaySeconds;
  document["alarm_active_high"] = gAlarmActiveHigh;
  document.remove("idle_delay_seconds");
  document.remove("idleDelaySeconds");
  for (uint8_t i = 0; i < 3; ++i) {
    String prefix = String(F("shift_")) + (i + 1);
    document[(prefix + F("_name")).c_str()] = gShiftName[i];
    document[(prefix + F("_start")).c_str()] = gShiftStart[i];
    document[(prefix + F("_end")).c_str()] = gShiftEnd[i];
  }

  File temp = LittleFS.open(kTempPath, "w");
  if (!temp) return false;
  const size_t written = serializeJsonPretty(document, temp);
  temp.flush();
  temp.close();
  if (written == 0) {
    LittleFS.remove(kTempPath);
    return false;
  }

  LittleFS.remove(kBackupPath);
  if (LittleFS.exists(kConfigPath) && !LittleFS.rename(kConfigPath, kBackupPath)) {
    LittleFS.remove(kTempPath);
    return false;
  }
  if (!LittleFS.rename(kTempPath, kConfigPath)) {
    if (LittleFS.exists(kBackupPath)) LittleFS.rename(kBackupPath, kConfigPath);
    LittleFS.remove(kTempPath);
    return false;
  }
  LittleFS.remove(kBackupPath);
  Logger::info(F("Configuration saved without removing extended fields"));
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
uint32_t Config::lossAlarmDelaySeconds() { return gLossAlarmDelaySeconds; }
uint16_t Config::hourlyUploadDelaySeconds() { return gHourlyUploadDelaySeconds; }
bool Config::alarmActiveHigh() { return gAlarmActiveHigh; }
