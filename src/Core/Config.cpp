#include "Config.h"
#include "Logger.h"
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <cstring>

namespace {
constexpr const char *kDevicePath = "/device.json";
constexpr const char *kServerPath = "/server.json";
constexpr const char *kLegacyPath = "/machine.json";
constexpr const char *kDeviceTempPath = "/device.json.tmp";
constexpr const char *kDeviceBackupPath = "/device.json.bak";
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

void copyValue(const char *value, char *dest, size_t size) { strlcpy(dest, value ? value : "", size); }
void assign(const String &value, char *dest, size_t size) { value.substring(0, size - 1).toCharArray(dest, size); }

bool loadJson(const char *path, DynamicJsonDocument &document) {
  File file = LittleFS.open(path, "r");
  if (!file) return false;
  const DeserializationError error = deserializeJson(document, file);
  file.close();
  return !error;
}

bool parseTime(const char *value, uint16_t &minutes) {
  if (!value || strlen(value) != 5 || value[2] != ':') return false;
  if (!isDigit(value[0]) || !isDigit(value[1]) || !isDigit(value[3]) || !isDigit(value[4])) return false;
  const uint8_t hour = static_cast<uint8_t>((value[0]-'0')*10 + value[1]-'0');
  const uint8_t minute = static_cast<uint8_t>((value[3]-'0')*10 + value[4]-'0');
  if (minute > 59 || hour > 24 || (hour == 24 && minute != 0)) return false;
  minutes = hour == 24 ? 0 : static_cast<uint16_t>(hour * 60U + minute);
  return true;
}

bool validateShiftSchedule() {
  for (uint8_t i=0;i<3;++i) {
    if (!gShiftName[i][0] || !parseTime(gShiftStart[i],gShiftStartMinutes[i]) || !parseTime(gShiftEnd[i],gShiftEndMinutes[i]) || gShiftStartMinutes[i]==gShiftEndMinutes[i]) return false;
  }
  if (!strcmp(gShiftName[0],gShiftName[1]) || !strcmp(gShiftName[0],gShiftName[2]) || !strcmp(gShiftName[1],gShiftName[2])) return false;
  return gShiftEndMinutes[0]==gShiftStartMinutes[1] && gShiftEndMinutes[1]==gShiftStartMinutes[2] && gShiftEndMinutes[2]==gShiftStartMinutes[0];
}

void readDevice(const JsonDocument &doc) {
  copyValue(doc["machine_id"] | gMachineId,gMachineId,sizeof(gMachineId));
  copyValue(doc["machine_name"] | gMachineName,gMachineName,sizeof(gMachineName));
  copyValue(doc["wifi_ssid"] | gWifiSsid,gWifiSsid,sizeof(gWifiSsid));
  copyValue(doc["wifi_password"] | gWifiPassword,gWifiPassword,sizeof(gWifiPassword));
  copyValue(doc["google_web_app_url"] | gGoogleWebAppUrl,gGoogleWebAppUrl,sizeof(gGoogleWebAppUrl));
  copyValue(doc["api_token"] | gApiToken,gApiToken,sizeof(gApiToken));
}

void readServer(const JsonDocument &doc) {
  uint32_t loss = doc["loss_alarm_delay_seconds"] | kDefaultLossAlarmDelaySeconds;
  if (loss < kMinLossAlarmDelaySeconds || loss > kMaxLossAlarmDelaySeconds) loss = kDefaultLossAlarmDelaySeconds;
  gLossAlarmDelaySeconds = loss;
  const uint32_t delay = doc["hourly_upload_delay_seconds"] | kDefaultHourlyUploadDelaySeconds;
  gHourlyUploadDelaySeconds = delay <= kMaxHourlyUploadDelaySeconds ? static_cast<uint16_t>(delay) : kDefaultHourlyUploadDelaySeconds;
  gAlarmActiveHigh = doc["alarm_active_high"] | true;
  for (uint8_t i=0;i<3;++i) {
    const String prefix = String(F("shift_")) + (i+1);
    copyValue(doc[(prefix+F("_name")).c_str()] | gShiftName[i],gShiftName[i],sizeof(gShiftName[i]));
    copyValue(doc[(prefix+F("_start")).c_str()] | gShiftStart[i],gShiftStart[i],sizeof(gShiftStart[i]));
    copyValue(doc[(prefix+F("_end")).c_str()] | gShiftEnd[i],gShiftEnd[i],sizeof(gShiftEnd[i]));
  }
}
}

bool Config::load() {
  DynamicJsonDocument device(8192), server(12288), legacy(12288);
  bool deviceOk = loadJson(kDevicePath,device);
  bool serverOk = loadJson(kServerPath,server);
  if ((!deviceOk || !serverOk) && loadJson(kLegacyPath,legacy)) {
    Logger::warn(F("[CONFIG] Legacy machine.json fallback active; upload device.json and server.json"));
    if (!deviceOk) { device.set(legacy); deviceOk = true; }
    if (!serverOk) { server.set(legacy); serverOk = true; }
  }
  if (deviceOk) readDevice(device); else Logger::warn(F("[CONFIG] device.json missing; using compiled device defaults"));
  if (serverOk) readServer(server); else Logger::warn(F("[CONFIG] server.json missing; using compiled server defaults"));
  gShiftScheduleValid = validateShiftSchedule();
  Logger::info(String(F("Config loaded for ")) + gMachineId + F(" using device.json + server.json"));
  if (!gShiftScheduleValid) Logger::error(F("[SHIFT] Invalid shift configuration; automatic shifts disabled"));
  return deviceOk || serverOk;
}

bool Config::save() {
  DynamicJsonDocument device(8192);
  loadJson(kDevicePath,device);
  device["machine_id"]=gMachineId; device["machine_name"]=gMachineName;
  device["wifi_ssid"]=gWifiSsid; device["wifi_password"]=gWifiPassword;
  device["google_web_app_url"]=gGoogleWebAppUrl; device["api_token"]=gApiToken;
  File temp=LittleFS.open(kDeviceTempPath,"w");
  if(!temp || serializeJsonPretty(device,temp)==0){if(temp)temp.close();LittleFS.remove(kDeviceTempPath);return false;}
  temp.flush();temp.close();LittleFS.remove(kDeviceBackupPath);
  if(LittleFS.exists(kDevicePath)&&!LittleFS.rename(kDevicePath,kDeviceBackupPath)){LittleFS.remove(kDeviceTempPath);return false;}
  if(!LittleFS.rename(kDeviceTempPath,kDevicePath)){if(LittleFS.exists(kDeviceBackupPath))LittleFS.rename(kDeviceBackupPath,kDevicePath);return false;}
  LittleFS.remove(kDeviceBackupPath);return true;
}

bool Config::update(const String &machineId,const String &machineName,const String &wifiSsid,const String &wifiPassword,const String &googleWebAppUrl){
  if(!machineId.length()||!machineName.length())return false;
  assign(machineId,gMachineId,sizeof(gMachineId));assign(machineName,gMachineName,sizeof(gMachineName));assign(wifiSsid,gWifiSsid,sizeof(gWifiSsid));assign(wifiPassword,gWifiPassword,sizeof(gWifiPassword));assign(googleWebAppUrl,gGoogleWebAppUrl,sizeof(gGoogleWebAppUrl));return save();
}
const char *Config::machineId(){return gMachineId;} const char *Config::machineName(){return gMachineName;} const char *Config::wifiSsid(){return gWifiSsid;} const char *Config::wifiPassword(){return gWifiPassword;} const char *Config::googleWebAppUrl(){return gGoogleWebAppUrl;} const char *Config::apiToken(){return gApiToken;}
const char *Config::shiftName(uint8_t i){return i<3?gShiftName[i]:"";} const char *Config::shiftStart(uint8_t i){return i<3?gShiftStart[i]:"";} const char *Config::shiftEnd(uint8_t i){return i<3?gShiftEnd[i]:"";}
uint16_t Config::shiftStartMinutes(uint8_t i){return i<3?gShiftStartMinutes[i]:0;} uint16_t Config::shiftEndMinutes(uint8_t i){return i<3?gShiftEndMinutes[i]:0;} bool Config::shiftScheduleValid(){return gShiftScheduleValid;}
uint32_t Config::lossAlarmDelaySeconds(){return gLossAlarmDelaySeconds;} uint16_t Config::hourlyUploadDelaySeconds(){return gHourlyUploadDelaySeconds;} bool Config::alarmActiveHigh(){return gAlarmActiveHigh;}
