#include "TelegramClient.h"
#include "NetworkBudget.h"
#include "WiFiManager.h"
#include "TimeManager.h"
#include "../Core/Config.h"
#include "../Core/LossCatalog.h"
#include "../Core/Logger.h"
#include "../Storage/ShiftCsvManager.h"
#include <ESP8266HTTPClient.h>
#include <WiFiClientSecureBearSSL.h>
#include <LittleFS.h>
#include <ArduinoJson.h>

namespace {
bool verified=false,verificationAttempted=false,readySent=false;
bool policyEnabled=true,sendReady=true,sendLossAlerts=true,sendReports=true;
uint32_t successCountValue=0,failureCountValue=0,lastVerifyMs=0,lastAttemptMs=0,lastSentLossMs=0;
uint16_t pendingLossCode=0,lastSentLossCode=0;
uint32_t pendingLossDuration=0,lastSentLossDuration=0;
String pendingReport;
char botToken[96]="",chatId[32]="";
constexpr uint32_t kVerifyRetryMs=60000UL,kMessageRetryMs=30000UL,kDuplicateMs=60000UL;
constexpr uint16_t kTelegramTimeoutMs=3000;

bool loadJson(const char *path,DynamicJsonDocument &doc){File f=LittleFS.open(path,"r");if(!f)return false;const auto e=deserializeJson(doc,f);f.close();return !e;}
void loadConfiguration(){botToken[0]='\0';chatId[0]='\0';DynamicJsonDocument device(4096),server(4096);if(loadJson("/device.json",device)){strlcpy(botToken,device["telegram_bot_token"]|"",sizeof(botToken));strlcpy(chatId,device["telegram_chat_id"]|"",sizeof(chatId));}if(loadJson("/server.json",server)){JsonObject p=server["communication"]["telegram"];policyEnabled=p["enabled"]|true;sendReady=p["sendMachineReady"]|true;sendLossAlerts=p["sendLossAlerts"]|true;sendReports=p["sendReports"]|true;}}
String encode(const String &v){String out;const char *hex="0123456789ABCDEF";for(size_t i=0;i<v.length();++i){const uint8_t c=v[i];if(isalnum(c)||c=='-'||c=='_'||c=='.'||c=='~')out+=char(c);else{out+='%';out+=hex[(c>>4)&15];out+=hex[c&15];}}return out;}
String api(const __FlashStringHelper *method){String u=F("https://api.telegram.org/bot");u+=botToken;u+='/';u+=method;return u;}

bool request(const String &url,String *bodyOut=nullptr){
  if(!WiFiManager::connected()||!NetworkBudget::acquire())return false;
  BearSSL::WiFiClientSecure client;client.setInsecure();client.setTimeout(kTelegramTimeoutMs);
  HTTPClient http;if(!http.begin(client,url))return false;http.setTimeout(kTelegramTimeoutMs);http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  const int code=http.GET();const String body=code>0?http.getString():String();http.end();if(bodyOut)*bodyOut=body;
  return code>=200&&code<300&&body.indexOf(F("\"ok\":true"))>=0;
}
bool verify(){if(!TelegramClient::configured())return false;String body;verified=request(api(F("getMe")),&body);verificationAttempted=true;lastVerifyMs=millis();return verified;}
bool sendText(const String &message){if(!TelegramClient::configured())return false;lastAttemptMs=millis();String url=api(F("sendMessage"));url+=F("?chat_id=");url+=encode(chatId);url+=F("&text=");url+=encode(message);const bool ok=request(url);ok?++successCountValue:++failureCountValue;return ok;}

bool sendDocument(const String &path){
  if(!TelegramClient::configured()||!sendReports||!NetworkBudget::acquire())return false;
  File file=LittleFS.open(path,"r");if(!file)return false;
  const String boundary=F("----AFMSBoundary7MA4YWxkTrZu0gW");String name=path;const int slash=name.lastIndexOf('/');if(slash>=0)name=name.substring(slash+1);
  String prefix=F("--");prefix+=boundary;prefix+=F("\r\nContent-Disposition: form-data; name=\"chat_id\"\r\n\r\n");prefix+=chatId;prefix+=F("\r\n--");prefix+=boundary;prefix+=F("\r\nContent-Disposition: form-data; name=\"document\"; filename=\"");prefix+=name;prefix+=F("\"\r\nContent-Type: text/csv\r\n\r\n");String suffix=F("\r\n--");suffix+=boundary;suffix+=F("--\r\n");
  BearSSL::WiFiClientSecure client;client.setInsecure();client.setTimeout(5000);if(!client.connect("api.telegram.org",443)){file.close();return false;}
  client.print(F("POST /bot"));client.print(botToken);client.println(F("/sendDocument HTTP/1.1"));client.println(F("Host: api.telegram.org"));client.print(F("Content-Type: multipart/form-data; boundary="));client.println(boundary);client.print(F("Content-Length: "));client.println(prefix.length()+file.size()+suffix.length());client.println(F("Connection: close\r\n"));client.print(prefix);
  uint8_t b[512];while(file.available()){const size_t n=file.read(b,sizeof(b));if(n)client.write(b,n);yield();}file.close();client.print(suffix);
  const uint32_t started=millis();while(!client.available()&&client.connected()&&millis()-started<5000UL)yield();String response;while(client.available())response+=char(client.read());client.stop();const bool ok=response.indexOf(F(" 200 "))>=0&&response.indexOf(F("\"ok\":true"))>=0;ok?++successCountValue:++failureCountValue;return ok;
}
}

void TelegramClient::begin(){verified=false;verificationAttempted=false;readySent=false;pendingLossCode=0;pendingLossDuration=0;pendingReport="";loadConfiguration();Logger::info(String(F("[TELEGRAM] policy="))+(policyEnabled?F("ON"):F("OFF")));}
void TelegramClient::update(){if(!configured()||!WiFiManager::connected())return;if(!verified){if(!verificationAttempted||millis()-lastVerifyMs>=kVerifyRetryMs)verify();if(!verified)return;}if(lastAttemptMs&&millis()-lastAttemptMs<kMessageRetryMs)return;if(sendReady&&!readySent){readySent=sendMachineReady();return;}if(!sendReady)readySent=true;if(sendLossAlerts&&pendingLossCode){if(sendLoss(pendingLossCode,pendingLossDuration)){pendingLossCode=0;pendingLossDuration=0;}return;}if(sendReports){if(!pendingReport.length())ShiftCsvManager::consumeDailyReportReady(pendingReport);if(pendingReport.length()&&sendDocument(pendingReport))pendingReport="";}}
bool TelegramClient::configured(){return policyEnabled&&botToken[0]&&chatId[0];}
bool TelegramClient::connected(){return verified&&configured();}
bool TelegramClient::sendMachineReady(){if(!sendReady)return true;String m=F("AFMS machine ready\nMachine: ");m+=Config::machineName();m+=F("\nMachine ID: ");m+=Config::machineId();m+=F("\nTime: ");m+=TimeManager::iso8601();return sendText(m);}
bool TelegramClient::sendLoss(uint16_t code,uint32_t duration){if(!sendLossAlerts)return true;const uint32_t now=millis();if(lastSentLossCode==code&&lastSentLossDuration==duration&&lastSentLossMs&&now-lastSentLossMs<kDuplicateMs)return true;String m=F("AFMS loss recorded\nMachine: ");m+=Config::machineName();m+=F("\nLoss: ");m+=LossCatalog::name(code);m+=F("\nLoss code: ");m+=code;m+=F("\nDuration: ");m+=duration;m+=F(" sec\nTime: ");m+=TimeManager::iso8601();if(!sendText(m))return false;lastSentLossCode=code;lastSentLossDuration=duration;lastSentLossMs=now;return true;}
void TelegramClient::queueLoss(uint16_t code,uint32_t duration){if(!policyEnabled||!sendLossAlerts||code<1||code>16)return;if(pendingLossCode==code&&pendingLossDuration==duration)return;pendingLossCode=code;pendingLossDuration=duration;}
uint32_t TelegramClient::successCount(){return successCountValue;}
uint32_t TelegramClient::failureCount(){return failureCountValue;}
