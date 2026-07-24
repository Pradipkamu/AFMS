#include "AfmsWebClient.h"
#include "CommunicationManager.h"
#include "WiFiManager.h"
#include "../Core/Config.h"
#include "../Core/Logger.h"
#include "../Core/Version.h"
#include "../Machine/MachineEngine.h"
#include "../Storage/OfflineQueue.h"
#include <ArduinoJson.h>
#include <ESP8266HTTPClient.h>
#include <ESP8266WiFi.h>
#include <LittleFS.h>

namespace {
uint32_t gSuccess=0,gFailure=0,gSequence=0,gLastParts=0;
uint16_t gLastLoss=0;
MachineState gLastState=MachineState::Ready;
bool gConnected=false,gObserved=false;

const char *statusName(MachineState state){
  switch(state){case MachineState::Running:return "RUNNING";case MachineState::Idle:return "IDLE";case MachineState::LossRequired:return "BREAKDOWN";default:return "STOPPED";}
}

void detectTriggers(){
  const MachineSnapshot m=MachineEngine::snapshot(); const uint16_t loss=MachineEngine::lastAcceptedLossCode();
  if(!gObserved){gObserved=true;gLastState=m.state;gLastParts=m.totalParts;gLastLoss=loss;return;}
  if(m.state!=gLastState){CommunicationManager::notify(CommunicationManager::Trigger::StatusChange);gLastState=m.state;}
  if(loss!=gLastLoss){CommunicationManager::notify(CommunicationManager::Trigger::LossChange);gLastLoss=loss;}
  const uint32_t milestone=CommunicationManager::productionMilestone();
  if(milestone&&m.totalParts/gMilestone!=gLastParts/gMilestone) CommunicationManager::notify(CommunicationManager::Trigger::ProductionMilestone);
  gLastParts=m.totalParts;
}

bool loadCredentials(String &baseUrl,String &apiKey){
  File file=LittleFS.open("/machine.json","r"); if(!file)return false;
  DynamicJsonDocument doc(12288); const auto error=deserializeJson(doc,file); file.close(); if(error)return false;
  baseUrl=doc["afms_server_url"]|""; apiKey=doc["device_api_key"]|"";
  return baseUrl.length()&&apiKey.length();
}

String buildPayload(String &eventId){
  const MachineSnapshot m=MachineEngine::snapshot();
  eventId=String(Config::machineId())+"-"+String(ESP.getChipId(),HEX)+"-"+String(millis())+"-"+String(++gSequence);
  DynamicJsonDocument doc(1024);
  doc["eventId"]=eventId; doc["machineId"]=Config::machineId(); doc["status"]=statusName(m.state);
  doc["productionCount"]=m.totalParts; doc["rejectCount"]=m.rejectParts; doc["cycleTimeMs"]=0;
  doc["lossCode"]=m.state==MachineState::LossRequired?String(MachineEngine::lastAcceptedLossCode()):String();
  doc["firmwareVersion"]=AFMS_VERSION;
  String payload; serializeJson(doc,payload); return payload;
}

bool sendPayload(const String &payload){
  String baseUrl,key; if(!loadCredentials(baseUrl,key))return false;
  WiFiClient client; HTTPClient http; const String url=baseUrl+"/api/v1/devices/telemetry";
  if(!http.begin(client,url))return false;
  http.addHeader("Content-Type","application/json"); http.addHeader("X-AFMS-Device-Key",key);
  const int code=http.POST(payload); http.end(); return code==200||code==201;
}
}

void AfmsWebClient::begin(){Logger::info(F("[AFMS] Authenticated telemetry sender ready"));}
void AfmsWebClient::update(){
  detectTriggers();
  if(!CommunicationManager::webDue())return;
  if(!WiFiManager::connected()){gConnected=false;return;}
  String eventId; const String payload=buildPayload(eventId);
  if(sendPayload(payload)){gConnected=true;++gSuccess;CommunicationManager::markWebComplete(true);}
  else{gConnected=false;++gFailure;OfflineQueue::enqueue(OfflineQueue::Destination::AfmsWeb,eventId,payload);CommunicationManager::markWebComplete(false);}
}
bool AfmsWebClient::connected(){return gConnected&&WiFiManager::connected();}
uint32_t AfmsWebClient::successCount(){return gSuccess;}
uint32_t AfmsWebClient::failureCount(){return gFailure;}
