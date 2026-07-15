#include "TelegramClient.h"
#include "WiFiManager.h"
#include "TimeManager.h"
#include "../Core/Config.h"
#include "../Core/Logger.h"
#include <ESP8266HTTPClient.h>
#include <WiFiClientSecureBearSSL.h>
#include <LittleFS.h>

namespace {
String gBotToken;
String gChatId;
bool gVerified = false;
bool gVerificationAttempted = false;
uint32_t gSuccess = 0;
uint32_t gFailure = 0;
uint32_t gLastVerifyAttemptMs = 0;
constexpr uint32_t kVerifyRetryMs = 60000UL;

String readJsonString(const String &json, const char *key) {
  const String token = String('"') + key + "\"";
  const int keyPos = json.indexOf(token);
  if (keyPos < 0) return String();
  const int colon = json.indexOf(':', keyPos + token.length());
  const int firstQuote = json.indexOf('"', colon + 1);
  const int secondQuote = json.indexOf('"', firstQuote + 1);
  if (colon < 0 || firstQuote < 0 || secondQuote < 0) return String();
  return json.substring(firstQuote + 1, secondQuote);
}

String urlEncode(const String &value) {
  String encoded;
  const char *hex = "0123456789ABCDEF";
  for (size_t i = 0; i < value.length(); ++i) {
    const uint8_t c = static_cast<uint8_t>(value[i]);
    if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') encoded += static_cast<char>(c);
    else { encoded += '%'; encoded += hex[(c >> 4) & 0x0F]; encoded += hex[c & 0x0F]; }
  }
  return encoded;
}

bool request(const String &url) {
  if (!WiFiManager::connected()) return false;
  BearSSL::WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  if (!http.begin(client, url)) return false;
  http.setTimeout(10000);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  const int code = http.GET();
  const String body = code > 0 ? http.getString() : String();
  http.end();
  return code >= 200 && code < 300 && body.indexOf(F("\"ok\":true")) >= 0;
}

String apiUrl(const __FlashStringHelper *method) {
  String url = F("https://api.telegram.org/bot");
  url += gBotToken;
  url += '/';
  url += method;
  return url;
}

bool sendText(const String &message) {
  if (!TelegramClient::configured() || !WiFiManager::connected()) return false;
  String url = apiUrl(F("sendMessage"));
  url += F("?chat_id="); url += urlEncode(gChatId);
  url += F("&text="); url += urlEncode(message);
  const bool ok = request(url);
  if (ok) { ++gSuccess; Logger::info(F("[TELEGRAM] Message sent")); }
  else { ++gFailure; Logger::warn(F("[TELEGRAM] Message failed")); }
  return ok;
}
}

void TelegramClient::begin() {
  File file = LittleFS.open("/machine.json", "r");
  if (file) {
    const String json = file.readString();
    file.close();
    gBotToken = readJsonString(json, "telegram_bot_token");
    gChatId = readJsonString(json, "telegram_chat_id");
  }
  if (!configured()) Logger::warn(F("[TELEGRAM] Bot token or chat ID missing"));
}

void TelegramClient::update() {
  if (!configured() || !WiFiManager::connected() || gVerified) return;
  if (gVerificationAttempted && millis() - gLastVerifyAttemptMs < kVerifyRetryMs) return;
  gVerificationAttempted = true;
  gLastVerifyAttemptMs = millis();
  gVerified = request(apiUrl(F("getMe")));
  if (gVerified) Logger::info(F("[TELEGRAM] Bot connected"));
  else Logger::warn(F("[TELEGRAM] Bot verification failed"));
}

bool TelegramClient::configured() { return gBotToken.length() && gChatId.length(); }
bool TelegramClient::connected() { return gVerified; }

bool TelegramClient::sendMachineReady() {
  String message = F("AFMS machine ready\nMachine: ");
  message += Config::machineName();
  message += F("\nMachine ID: "); message += Config::machineId();
  message += F("\nTime: "); message += TimeManager::iso8601();
  return sendText(message);
}

bool TelegramClient::sendLoss(uint16_t lossCode, uint32_t durationSeconds) {
  String message = F("AFMS loss recorded\nMachine: ");
  message += Config::machineName();
  message += F("\nLoss code: "); message += lossCode;
  message += F("\nDuration: "); message += durationSeconds;
  message += F(" sec\nTime: "); message += TimeManager::iso8601();
  return sendText(message);
}

uint32_t TelegramClient::successCount() { return gSuccess; }
uint32_t TelegramClient::failureCount() { return gFailure; }
