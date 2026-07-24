#include "RemoteConfigManager.h"
#include "../Core/Config.h"
#include "../Core/Logger.h"
#include <ArduinoJson.h>
#include <ESP8266HTTPClient.h>
#include <ESP8266WiFi.h>
#include <LittleFS.h>
#include <WiFiClient.h>

namespace {
constexpr const char *kDevicePath="/device.json";
constexpr const char *kServerPath="/server.json";
constexpr const char *kTempPath="/server.remote.tmp";
constexpr const char *kBackupPath="/server.remote.bak";
constexpr uint32_t kDefaultCheckMs=300000UL;
uint32_t gLastCheck=0,gVersion=0;bool gUsingRemote=false;char gError[80]="not checked";
void setError(const String &value){value.substring(0,sizeof(gError)-1).toCharArray(gError,sizeof(gError));}
bool readFile(const char *path,DynamicJsonDocument &doc){File file=LittleFS.open(path,"r");if(!file)return false;const auto error=deserializeJson(doc,file);file.close();return !error;}
bool loadLocalSettings(String &serverUrl,String &apiKey,uint32_t &intervalMs,bool &enabled){
  DynamicJsonDocument device(8192),server(12288);if(!readFile(kDevicePath,device))return false;readFile(kServerPath,server);
  serverUrl=device["afms_server_url"]|"";apiKey=device["device_api_key"]|"";gVersion=server["configVersion"]|0;
  enabled=server["communication"]["configSync"]["enabled"]|true;uint32_t seconds=server["communication"]["configSync"]["checkIntervalSeconds"]|300;seconds=constrain(seconds,60UL,86400UL);intervalMs=seconds*1000UL;return serverUrl.length()>0;
}
bool validateRemote(const JsonObjectConst config){const JsonObjectConst web=config["communication"]["afmsWeb"];const JsonObjectConst google=config["communication"]["googleSheets"];const char *mode=web["mode"]|"HYBRID";if(strcmp(mode,"INTERVAL")&&strcmp(mode,"CYCLE")&&strcmp(mode,"HYBRID"))return false;const uint32_t interval=web["intervalSeconds"]|60,heartbeat=web["heartbeatSeconds"]|60,milestone=web["productionMilestone"]|10,googleInterval=google["uploadIntervalSeconds"]|3600;return interval>=5&&interval<=3600&&heartbeat>=15&&heartbeat<=3600&&milestone>=1&&milestone<=10000&&googleInterval>=60&&googleInterval<=86400;}
bool applyRemote(const JsonObjectConst response){
  const uint32_t version=response["configVersion"]|0;const JsonObjectConst remote=response["configuration"];
  if(!version||remote.isNull()||!validateRemote(remote)){setError("remote config rejected");return false;}
  DynamicJsonDocument server(12288);readFile(kServerPath,server);server.set(remote);server["configVersion"]=version;server["updatedAt"]=response["updatedAt"]|"";
  File temp=LittleFS.open(kTempPath,"w");if(!temp||serializeJsonPretty(server,temp)==0){if(temp)temp.close();LittleFS.remove(kTempPath);setError("unable to save server config");return false;}temp.flush();temp.close();
  LittleFS.remove(kBackupPath);if(LittleFS.exists(kServerPath)&&!LittleFS.rename(kServerPath,kBackupPath)){LittleFS.remove(kTempPath);setError("server config backup failed");return false;}
  if(!LittleFS.rename(kTempPath,kServerPath)){if(LittleFS.exists(kBackupPath))LittleFS.rename(kBackupPath,kServerPath);setError("server config activation failed");return false;}
  LittleFS.remove(kBackupPath);gVersion=version;gUsingRemote=Config::load();setError(gUsingRemote?"none":"saved but reload failed");return gUsingRemote;
}
void checkNow(){if(WiFi.status()!=WL_CONNECTED){setError("offline; using local server.json");return;}String serverUrl,apiKey;uint32_t intervalMs=kDefaultCheckMs;bool enabled=true;if(!loadLocalSettings(serverUrl,apiKey,intervalMs,enabled)){setError("device.json server URL not configured");return;}if(!enabled){setError("config sync disabled");return;}WiFiClient client;HTTPClient http;String url=serverUrl+"/api/v1/devices/"+Config::machineId()+"/config?currentVersion="+String(gVersion);if(!http.begin(client,url)){setError("HTTP begin failed");return;}if(apiKey.length())http.addHeader("X-AFMS-Device-Key",apiKey);const int code=http.GET();if(code==HTTP_CODE_NOT_MODIFIED){setError("none");http.end();return;}if(code!=HTTP_CODE_OK){setError(String("config HTTP ")+code);http.end();return;}DynamicJsonDocument response(12288);const auto error=deserializeJson(response,http.getStream());http.end();if(error){setError("invalid config JSON");return;}applyRemote(response.as<JsonObjectConst>());}
}
void RemoteConfigManager::begin(){gLastCheck=millis()-kDefaultCheckMs;Logger::info(F("[CONFIG] Remote server.json manager ready"));}
void RemoteConfigManager::update(){String url,key;uint32_t intervalMs=kDefaultCheckMs;bool enabled=true;if(!loadLocalSettings(url,key,intervalMs,enabled)||!enabled)return;if(millis()-gLastCheck>=intervalMs){gLastCheck=millis();checkNow();}}
uint32_t RemoteConfigManager::activeVersion(){return gVersion;}bool RemoteConfigManager::usingRemoteConfig(){return gUsingRemote;}const char *RemoteConfigManager::lastError(){return gError;}
