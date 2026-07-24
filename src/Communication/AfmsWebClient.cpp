#include "AfmsWebClient.h"
#include "CommunicationManager.h"
#include "WiFiManager.h"
#include "TimeManager.h"
#include "../Core/Config.h"
#include "../Core/LossCatalog.h"
#include "../Core/Logger.h"
#include "../Core/Version.h"
#include "../Machine/MachineEngine.h"
#include "../Machine/ShiftManager.h"
#include "../Storage/OfflineQueue.h"
#include <ArduinoJson.h>
#include <ESP8266HTTPClient.h>
#include <ESP8266WiFi.h>
#include <WiFiClientSecureBearSSL.h>
#include <LittleFS.h>

namespace {
uint32_t okCount=0,failCount=0,sequence=0,lastParts=0,nextReplay=0,previousPulseMs=0,lastCycleMs=0;
uint16_t lastLoss=0;uint8_t replayFailures=0;MachineState lastState=MachineState::Ready;bool connectedFlag=false,observed=false;
const char *statusName(MachineState s){return s==MachineState::Running?"RUNNING":s==MachineState::Idle?"IDLE":s==MachineState::LossRequired?"BREAKDOWN":"STOPPED";}
bool loadJson(const char *path,DynamicJsonDocument &doc){File f=LittleFS.open(path,"r");if(!f)return false;const auto e=deserializeJson(doc,f);f.close();return !e;}
void detectTriggers(){const MachineSnapshot m=MachineEngine::snapshot();const uint16_t loss=MachineEngine::lastAcceptedLossCode();if(!observed){observed=true;lastState=m.state;lastParts=m.totalParts;lastLoss=loss;previousPulseMs=m.lastProductionMs;return;}if(m.state!=lastState){CommunicationManager::notify(CommunicationManager::Trigger::StatusChange);lastState=m.state;}if(loss!=lastLoss){CommunicationManager::notify(CommunicationManager::Trigger::LossChange);lastLoss=loss;}if(m.totalParts!=lastParts&&m.lastProductionMs!=previousPulseMs){if(previousPulseMs&&m.lastProductionMs>previousPulseMs)lastCycleMs=m.lastProductionMs-previousPulseMs;previousPulseMs=m.lastProductionMs;}const uint32_t milestone=CommunicationManager::productionMilestone();if(milestone&&m.totalParts/milestone!=lastParts/milestone)CommunicationManager::notify(CommunicationManager::Trigger::ProductionMilestone);lastParts=m.totalParts;}
bool credentials(String &base,String &key,String &fingerprint,bool &requireHttps){DynamicJsonDocument d(8192),s(4096);if(!loadJson("/device.json",d))return false;loadJson("/server.json",s);base=d["afms_server_url"]|"";key=d["device_api_key"]|"";fingerprint=d["afms_tls_fingerprint"]|"";requireHttps=s["communication"]["afmsWeb"]["requireHttps"]|false;return base.length()&&key.length();}
String payload(String &eventId){const MachineSnapshot m=MachineEngine::snapshot();const ShiftSnapshot s=ShiftManager::snapshot();const uint16_t loss=MachineEngine::lastAcceptedLossCode();eventId=String(Config::machineId())+"-"+String(ESP.getChipId(),HEX)+"-"+String(millis())+"-"+String(++sequence);DynamicJsonDocument d(2048);d["eventId"]=eventId;d["machineId"]=Config::machineId();d["machineName"]=Config::machineName();d["timestamp"]=TimeManager::iso8601();d["status"]=statusName(m.state);d["productionCount"]=m.totalParts;d["rejectCount"]=m.rejectParts;d["goodCount"]=m.goodParts;d["cycleTimeMs"]=lastCycleMs;d["lossCode"]=loss;d["lossName"]=loss?LossCatalog::name(loss):"";d["lossDurationSeconds"]=MachineEngine::lastLossDurationSeconds();d["shiftId"]=s.shiftId;d["operatorId"]=s.operatorId;d["partNumber"]=s.partNumber;d["partName"]=s.partName;d["alarmActive"]=m.alarmActive;d["idleSeconds"]=m.idleSeconds;d["runSeconds"]=m.runSeconds;d["downtimeSeconds"]=m.downtimeSeconds;d["availabilityPermille"]=m.availabilityPermille;d["performancePermille"]=m.performancePermille;d["qualityPermille"]=m.qualityPermille;d["oeePermille"]=m.oeePermille;d["wifiRssi"]=WiFi.RSSI();d["freeHeap"]=ESP.getFreeHeap();d["firmwareVersion"]=AFMS_VERSION;String out;serializeJson(d,out);return out;}
bool send(const String &body){String base,key,fingerprint;bool requireHttps=false;if(!credentials(base,key,fingerprint,requireHttps))return false;const bool https=base.startsWith("https://");if(requireHttps&&!https)return false;HTTPClient http;const String url=base+"/api/v1/devices/telemetry";int code=-1;if(https){if(!fingerprint.length())return false;BearSSL::WiFiClientSecure client;client.setFingerprint(fingerprint.c_str());if(!http.begin(client,url))return false;http.addHeader("Content-Type","application/json");http.addHeader("X-AFMS-Device-Key",key);code=http.POST(body);http.end();}else{WiFiClient client;if(!http.begin(client,url))return false;http.addHeader("Content-Type","application/json");http.addHeader("X-AFMS-Device-Key",key);code=http.POST(body);http.end();}return code==200||code==201;}
void replay(){if(!WiFiManager::connected()||static_cast<int32_t>(millis()-nextReplay)<0)return;String id,body;if(!OfflineQueue::peek(OfflineQueue::Destination::AfmsWeb,id,body)){replayFailures=0;return;}if(send(body)){OfflineQueue::removeHead(OfflineQueue::Destination::AfmsWeb);replayFailures=0;nextReplay=millis()+1000UL;++okCount;}else{if(replayFailures<6)++replayFailures;uint32_t wait=5000UL<<replayFailures;if(wait>300000UL)wait=300000UL;nextReplay=millis()+wait;++failCount;}}
}
void AfmsWebClient::begin(){Logger::info(F("[AFMS] Authenticated telemetry sender ready"));}
void AfmsWebClient::update(){detectTriggers();replay();if(!CommunicationManager::webDue())return;if(!WiFiManager::connected()){connectedFlag=false;return;}String id;const String body=payload(id);if(send(body)){connectedFlag=true;++okCount;CommunicationManager::markWebComplete(true);}else{connectedFlag=false;++failCount;OfflineQueue::enqueue(OfflineQueue::Destination::AfmsWeb,id,body);CommunicationManager::markWebComplete(false);}}
bool AfmsWebClient::connected(){return connectedFlag&&WiFiManager::connected();}uint32_t AfmsWebClient::successCount(){return okCount;}uint32_t AfmsWebClient::failureCount(){return failCount;}
