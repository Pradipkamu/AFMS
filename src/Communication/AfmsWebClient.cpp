#include "AfmsWebClient.h"
#include "CommunicationManager.h"
#include "NetworkBudget.h"
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
uint32_t okCount=0,failCount=0,sequence=0,lastParts=0,nextReplay=0,previousPulseMs=0,lastCycleMs=0,lastCredentialLoadMs=0;
uint16_t lastLoss=0;
uint8_t replayFailures=0;
MachineState lastState=MachineState::Ready;
bool connectedFlag=false,observed=false;
String cachedBase,cachedKey,cachedFingerprint;
bool cachedRequireHttps=false,credentialsValid=false;

const char *statusName(MachineState s){return s==MachineState::Running?"RUNNING":s==MachineState::Idle?"IDLE":s==MachineState::LossRequired?"BREAKDOWN":"STOPPED";}
bool loadJson(const char *path,DynamicJsonDocument &doc){File f=LittleFS.open(path,"r");if(!f)return false;const auto e=deserializeJson(doc,f);f.close();return !e;}

void loadCredentials(){
  DynamicJsonDocument d(8192),s(4096);
  credentialsValid=false;cachedBase="";cachedKey="";cachedFingerprint="";cachedRequireHttps=false;
  if(!loadJson("/device.json",d))return;
  loadJson("/server.json",s);
  cachedBase=d["afms_server_url"]|"";
  cachedKey=d["device_api_key"]|"";
  cachedFingerprint=d["afms_tls_fingerprint"]|"";
  cachedRequireHttps=s["communication"]["afmsWeb"]["requireHttps"]|false;
  credentialsValid=cachedBase.length()&&cachedKey.length();
  lastCredentialLoadMs=millis();
}

void detectTriggers(){
  const MachineSnapshot m=MachineEngine::snapshot();const uint16_t loss=MachineEngine::lastAcceptedLossCode();
  if(!observed){observed=true;lastState=m.state;lastParts=m.totalParts;lastLoss=loss;previousPulseMs=m.lastProductionMs;return;}
  if(m.state!=lastState){CommunicationManager::notify(CommunicationManager::Trigger::StatusChange);lastState=m.state;}
  if(loss!=lastLoss){CommunicationManager::notify(CommunicationManager::Trigger::LossChange);lastLoss=loss;}
  if(m.totalParts!=lastParts&&m.lastProductionMs!=previousPulseMs){if(previousPulseMs&&m.lastProductionMs>previousPulseMs)lastCycleMs=m.lastProductionMs-previousPulseMs;previousPulseMs=m.lastProductionMs;}
  const uint32_t milestone=CommunicationManager::productionMilestone();
  if(milestone&&m.totalParts/milestone!=lastParts/milestone)CommunicationManager::notify(CommunicationManager::Trigger::ProductionMilestone);
  lastParts=m.totalParts;
}

String payload(String &eventId){
  const MachineSnapshot m=MachineEngine::snapshot();const ShiftSnapshot s=ShiftManager::snapshot();const uint16_t loss=MachineEngine::lastAcceptedLossCode();
  eventId=String(Config::machineId())+"-"+String(ESP.getChipId(),HEX)+"-"+String(millis())+"-"+String(++sequence);
  DynamicJsonDocument d(2048);
  d["eventId"]=eventId;d["machineId"]=Config::machineId();d["machineName"]=Config::machineName();d["timestamp"]=TimeManager::iso8601();d["status"]=statusName(m.state);
  d["productionCount"]=m.totalParts;d["rejectCount"]=m.rejectParts;d["goodCount"]=m.goodParts;d["cycleTimeMs"]=lastCycleMs;
  d["lossCode"]=loss;d["lossName"]=loss?LossCatalog::name(loss):"";d["lossDurationSeconds"]=MachineEngine::lastLossDurationSeconds();
  d["shiftId"]=s.shiftId;d["operatorId"]=s.operatorId;d["partNumber"]=s.partNumber;d["partName"]=s.partName;d["alarmActive"]=m.alarmActive;
  d["idleSeconds"]=m.idleSeconds;d["runSeconds"]=m.runSeconds;d["downtimeSeconds"]=m.downtimeSeconds;
  d["availabilityPermille"]=m.availabilityPermille;d["performancePermille"]=m.performancePermille;d["qualityPermille"]=m.qualityPermille;d["oeePermille"]=m.oeePermille;
  d["wifiRssi"]=WiFi.RSSI();d["freeHeap"]=ESP.getFreeHeap();d["firmwareVersion"]=AFMS_VERSION;
  String out;serializeJson(d,out);return out;
}

// 1=success, 0=network failure, -1=deferred because another network task ran.
int8_t send(const String &body){
  if(!credentialsValid||millis()-lastCredentialLoadMs>=60000UL)loadCredentials();
  if(!credentialsValid)return 0;
  if(!NetworkBudget::acquire())return -1;
  const bool https=cachedBase.startsWith("https://");
  if(cachedRequireHttps&&!https)return 0;
  HTTPClient http;const String url=cachedBase+"/api/v1/devices/telemetry";int code=-1;
  if(https){
    if(!cachedFingerprint.length())return 0;
    BearSSL::WiFiClientSecure client;client.setFingerprint(cachedFingerprint.c_str());client.setTimeout(3000);
    if(!http.begin(client,url))return 0;http.setTimeout(3000);http.addHeader("Content-Type","application/json");http.addHeader("X-AFMS-Device-Key",cachedKey);code=http.POST(body);http.end();
  }else{
    WiFiClient client;client.setTimeout(3000);
    if(!http.begin(client,url))return 0;http.setTimeout(3000);http.addHeader("Content-Type","application/json");http.addHeader("X-AFMS-Device-Key",cachedKey);code=http.POST(body);http.end();
  }
  return (code==200||code==201)?1:0;
}

void replay(){
  if(!WiFiManager::connected()||static_cast<int32_t>(millis()-nextReplay)<0)return;
  String id,body;if(!OfflineQueue::peek(OfflineQueue::Destination::AfmsWeb,id,body)){replayFailures=0;return;}
  const int8_t result=send(body);
  if(result<0)return;
  if(result>0){OfflineQueue::removeHead(OfflineQueue::Destination::AfmsWeb);replayFailures=0;nextReplay=millis()+1000UL;++okCount;}
  else{if(replayFailures<6)++replayFailures;uint32_t wait=5000UL<<replayFailures;if(wait>300000UL)wait=300000UL;nextReplay=millis()+wait;++failCount;}
}
}

void AfmsWebClient::begin(){loadCredentials();Logger::info(F("[AFMS] Prioritized telemetry sender ready"));}
void AfmsWebClient::update(){
  detectTriggers();
  if(!WiFiManager::connected()){connectedFlag=false;return;}

  // Send current status/loss first. Historical replay only runs when no fresh item is due.
  if(CommunicationManager::webDue()){
    String id;const String body=payload(id);const int8_t result=send(body);
    if(result<0)return;
    if(result>0){connectedFlag=true;++okCount;CommunicationManager::markWebComplete(true);}
    else{connectedFlag=false;++failCount;OfflineQueue::enqueue(OfflineQueue::Destination::AfmsWeb,id,body);CommunicationManager::markWebComplete(false);}
    return;
  }
  replay();
}
bool AfmsWebClient::connected(){return connectedFlag&&WiFiManager::connected();}
uint32_t AfmsWebClient::successCount(){return okCount;}
uint32_t AfmsWebClient::failureCount(){return failCount;}
