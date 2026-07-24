#include "CloudManager.h"
#include "CommunicationManager.h"
#include "HttpClientManager.h"
#include "TimeManager.h"
#include "WiFiManager.h"
#include "TelegramClient.h"
#include "../Core/Config.h"
#include "../Core/EventBus.h"
#include "../Core/LossCatalog.h"
#include "../Core/Logger.h"
#include "../Machine/MachineEngine.h"
#include "../Machine/ShiftManager.h"
#include "../Storage/OfflineQueue.h"
#include <LittleFS.h>

namespace {
constexpr const char *kPendingPath = "/google.pending";
constexpr const char *kPendingTempPath = "/google.pending.tmp";
uint32_t gLastPeriodBucket = 0;
bool gScheduleInitialized = false;
String gPendingPayload;
uint32_t gPendingNotBeforeEpoch = 0;
uint32_t gSuccess = 0, gFailure = 0, gNextQueueRetryMs = 0;
uint8_t gQueueFailureStreak = 0;
bool gConnected = false;
constexpr uint32_t kQueueRetryBaseMs = 30000UL;
constexpr uint32_t kQueueRetryMaxMs = 900000UL;
constexpr uint32_t kQueueSuccessCooldownMs = 1000UL;

String jsonEscape(const char *value){String out;if(!value)return out;while(*value){if(*value=='"'||*value=='\\')out+='\\';out+=*value++;}return out;}
String jsonEscape(const String &value){return jsonEscape(value.c_str());}
const __FlashStringHelper *eventName(EventType type){switch(type){case EventType::MachineReady:return F("machine_ready");case EventType::LossSelected:return F("loss_selected");default:return F("ignored");}}

String buildSummaryPayload(uint32_t periodEndEpoch){
  const MachineSnapshot machine=MachineEngine::snapshot();const ShiftSnapshot shift=ShiftManager::snapshot();String payload;payload.reserve(900);
  payload+=F("{\"record_type\":\"hourly_summary\",\"api_token\":\"");payload+=jsonEscape(Config::apiToken());
  payload+=F("\",\"machine_id\":\"");payload+=jsonEscape(Config::machineId());payload+=F("\",\"machine_name\":\"");payload+=jsonEscape(Config::machineName());
  payload+=F("\",\"timestamp\":\"");payload+=TimeManager::iso8601();payload+=F("\",\"period_end_epoch\":");payload+=periodEndEpoch;
  payload+=F(",\"upload_delay_seconds\":");payload+=Config::hourlyUploadDelaySeconds();payload+=F(",\"state\":");payload+=static_cast<uint8_t>(machine.state);
  payload+=F(",\"shift\":");payload+=shift.shiftId;payload+=F(",\"operator_id\":");payload+=shift.operatorId;payload+=F(",\"part_number\":");payload+=shift.partNumber;
  payload+=F(",\"part_name\":\"");payload+=jsonEscape(shift.partName);payload+=F("\",\"total\":");payload+=machine.totalParts;payload+=F(",\"reject\":");payload+=machine.rejectParts;
  payload+=F(",\"good\":");payload+=machine.goodParts;payload+=F(",\"shift_production\":");payload+=shift.production;payload+=F(",\"shift_reject\":");payload+=shift.reject;
  payload+=F(",\"shift_good\":");payload+=shift.good;payload+=F(",\"target\":");payload+=shift.targetQuantity;payload+=F(",\"idle_seconds\":");payload+=machine.idleSeconds;
  payload+=F(",\"run_seconds\":");payload+=machine.runSeconds;payload+=F(",\"downtime_seconds\":");payload+=machine.downtimeSeconds;payload+=F(",\"availability_permille\":");payload+=machine.availabilityPermille;
  payload+=F(",\"performance_permille\":");payload+=machine.performancePermille;payload+=F(",\"quality_permille\":");payload+=machine.qualityPermille;payload+=F(",\"oee_permille\":");payload+=machine.oeePermille;
  payload+=F(",\"alarm\":");payload+=machine.alarmActive?F("true"):F("false");payload+='}';return payload;
}

String buildEventPayload(const Event &event){
  const MachineSnapshot machine=MachineEngine::snapshot();const ShiftSnapshot shift=ShiftManager::snapshot();String payload;payload.reserve(700);
  payload+=F("{\"record_type\":\"event\",\"api_token\":\"");payload+=jsonEscape(Config::apiToken());payload+=F("\",\"machine_id\":\"");payload+=jsonEscape(Config::machineId());
  payload+=F("\",\"machine_name\":\"");payload+=jsonEscape(Config::machineName());payload+=F("\",\"timestamp\":\"");payload+=TimeManager::iso8601();payload+=F("\",\"event_name\":\"");payload+=eventName(event.type);
  payload+=F("\",\"event_value\":");payload+=event.value;payload+=F(",\"duration_seconds\":");payload+=event.durationSeconds;
  if(event.type==EventType::LossSelected){const uint16_t lossCode=static_cast<uint16_t>(event.value);payload+=F(",\"loss_code\":");payload+=lossCode;payload+=F(",\"loss_name\":\"");payload+=jsonEscape(LossCatalog::name(lossCode));payload+=F("\",\"loss_duration_seconds\":");payload+=event.durationSeconds;}
  payload+=F(",\"state\":");payload+=static_cast<uint8_t>(machine.state);payload+=F(",\"shift\":");payload+=shift.shiftId;payload+=F(",\"operator_id\":");payload+=shift.operatorId;
  payload+=F(",\"part_number\":");payload+=shift.partNumber;payload+=F(",\"part_name\":\"");payload+=jsonEscape(shift.partName);payload+=F("\",\"total\":");payload+=machine.totalParts;
  payload+=F(",\"reject\":");payload+=machine.rejectParts;payload+=F(",\"good\":");payload+=machine.goodParts;payload+=F(",\"alarm\":");payload+=machine.alarmActive?F("true"):F("false");payload+='}';return payload;
}

// 1 = uploaded, 0 = real failure, -1 = deferred because another network task used this loop.
int8_t upload(const String &payload){
  if(!CommunicationManager::googleEnabled())return 0;
  const char *url=Config::googleWebAppUrl();
  if(!url||!url[0]){gConnected=false;Logger::warn(F("[GOOGLE] Upload skipped: Web App URL missing"));return 0;}
  const HttpResult result=HttpClientManager::postJson(url,payload);
  if(result.code==-3)return -1;
  Logger::info(String(F("[GOOGLE] HTTP status: "))+result.code);
  if(result.success()){gConnected=true;++gSuccess;return 1;}
  gConnected=false;++gFailure;return 0;
}

bool queuePayload(const String &payload){if(!CommunicationManager::googleEnabled())return false;if(OfflineQueue::push(payload))return true;++gFailure;Logger::error(F("[GOOGLE] Failed to queue payload"));return false;}

bool parsePending(const String &line,uint32_t &notBefore,String &payload){const int separator=line.indexOf('\t');if(separator<=0)return false;notBefore=static_cast<uint32_t>(strtoul(line.substring(0,separator).c_str(),nullptr,10));payload=line.substring(separator+1);return notBefore&&payload.length();}
bool loadFirstPending(){gPendingPayload="";gPendingNotBeforeEpoch=0;File file=LittleFS.open(kPendingPath,"r");if(!file||!file.available()){if(file)file.close();return false;}const String line=file.readStringUntil('\n');file.close();return parsePending(line,gPendingNotBeforeEpoch,gPendingPayload);}
bool appendPending(uint32_t notBefore,const String &payload){File file=LittleFS.open(kPendingPath,"a");if(!file)return false;file.print(notBefore);file.print('\t');const bool ok=file.println(payload)>0;file.close();if(ok&&!gPendingPayload.length()){gPendingNotBeforeEpoch=notBefore;gPendingPayload=payload;}return ok;}
bool removeFirstPending(){File source=LittleFS.open(kPendingPath,"r");if(!source||!source.available()){if(source)source.close();return false;}source.readStringUntil('\n');File temp=LittleFS.open(kPendingTempPath,"w");if(!temp){source.close();return false;}while(source.available())temp.write(source.read());source.close();temp.close();LittleFS.remove(kPendingPath);if(!LittleFS.rename(kPendingTempPath,kPendingPath))return false;loadFirstPending();return true;}
void releasePending(){if(!CommunicationManager::googleEnabled()||!gPendingPayload.length()||!TimeManager::synchronized())return;const time_t now=TimeManager::now();if(now<=0||static_cast<uint32_t>(now)<gPendingNotBeforeEpoch)return;if(queuePayload(gPendingPayload))removeFirstPending();}

void schedulePeriodicSummary(){
  if(!CommunicationManager::googleEnabled()||!TimeManager::synchronized())return;
  const time_t now=TimeManager::now();if(now<=0)return;
  const uint32_t interval=CommunicationManager::googleIntervalSeconds();
  const uint32_t epochNow=static_cast<uint32_t>(now);const uint32_t bucket=epochNow/interval;
  if(!gScheduleInitialized){gLastPeriodBucket=bucket;gScheduleInitialized=true;return;}
  if(bucket==gLastPeriodBucket)return;
  const uint32_t periodEndEpoch=bucket*interval;const uint32_t notBefore=periodEndEpoch+Config::hourlyUploadDelaySeconds();
  if(appendPending(notBefore,buildSummaryPayload(periodEndEpoch)))gLastPeriodBucket=bucket;else ++gFailure;
}

uint32_t retryDelay(){const uint8_t exponent=gQueueFailureStreak>5?5:gQueueFailureStreak;uint32_t delayMs=kQueueRetryBaseMs<<exponent;return delayMs>kQueueRetryMaxMs?kQueueRetryMaxMs:delayMs;}
void processQueuedUpload(){
  if(!CommunicationManager::googleEnabled()){gConnected=false;return;}
  if(!WiFiManager::connected()){gConnected=false;return;}
  String queued;
  if(!OfflineQueue::peek(queued)){gQueueFailureStreak=0;gNextQueueRetryMs=0;return;}
  const uint32_t now=millis();
  if(gNextQueueRetryMs&&static_cast<int32_t>(now-gNextQueueRetryMs)<0)return;
  const int8_t result=upload(queued);
  if(result<0)return;
  if(result>0){OfflineQueue::pop();gQueueFailureStreak=0;gNextQueueRetryMs=now+kQueueSuccessCooldownMs;Logger::info(F("[GOOGLE] Queued upload successful"));}
  else{if(gQueueFailureStreak<255)++gQueueFailureStreak;const uint32_t delayMs=retryDelay();gNextQueueRetryMs=now+delayMs;Logger::warn(String(F("[GOOGLE] Retry in "))+(delayMs/1000UL)+F(" sec"));}
}
}

void CloudManager::begin(){HttpClientManager::begin(10000);TimeManager::begin();OfflineQueue::begin();TelegramClient::begin();loadFirstPending();gLastPeriodBucket=0;gScheduleInitialized=false;gConnected=false;gNextQueueRetryMs=0;gQueueFailureStreak=0;}
void CloudManager::update(){
  TimeManager::update();TelegramClient::update();if(!WiFiManager::connected())gConnected=false;
  Event event;while(EventBus::next(event)){
    if(event.type==EventType::LossSelected&&event.value>=1&&event.value<=16)TelegramClient::queueLoss(static_cast<uint16_t>(event.value),event.durationSeconds);
    if(!CommunicationManager::googleEnabled())continue;
    if(event.type==EventType::MachineReady)queuePayload(buildEventPayload(event));
    else if(event.type==EventType::LossSelected&&CommunicationManager::googleBreakdownImmediate()){
      if(queuePayload(buildEventPayload(event)))Logger::info(String(F("[GOOGLE] Loss queued: code "))+event.value+F(", duration ")+event.durationSeconds+F(" sec"));
    }
  }
  if(CommunicationManager::googleEnabled()){
    String shiftSummary;if(ShiftManager::consumeCompletedSummary(shiftSummary))queuePayload(shiftSummary);
    releasePending();schedulePeriodicSummary();releasePending();processQueuedUpload();
  }
}
void CloudManager::queueStatusNow(){if(!CommunicationManager::googleEnabled())return;const time_t now=TimeManager::now();queuePayload(buildSummaryPayload(now>0?static_cast<uint32_t>(now):0));}
bool CloudManager::connected(){return CommunicationManager::googleEnabled()&&gConnected&&WiFiManager::connected();}
uint32_t CloudManager::uploadSuccessCount(){return gSuccess;}
uint32_t CloudManager::uploadFailureCount(){return gFailure;}
