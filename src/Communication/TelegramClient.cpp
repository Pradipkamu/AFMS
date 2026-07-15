#include "TelegramClient.h"
#include "WiFiManager.h"
#include "TimeManager.h"
#include "../Core/Config.h"
#include "../Core/Logger.h"
#include "../Storage/ShiftCsvManager.h"
#include <ESP8266HTTPClient.h>
#include <WiFiClientSecureBearSSL.h>
#include <LittleFS.h>

namespace {
bool gVerified = false;
bool gVerificationAttempted = false;
bool gReadyMessageSent = false;
uint32_t gSuccess = 0;
uint32_t gFailure = 0;
uint32_t gLastVerifyAttemptMs = 0;
uint32_t gLastMessageAttemptMs = 0;
uint16_t gPendingLossCode = 0;
uint32_t gPendingLossDurationSeconds = 0;
String gPendingMonthlyReport;
constexpr uint32_t kVerifyRetryMs = 60000UL;
constexpr uint32_t kMessageRetryMs = 30000UL;
char gBotToken[96] = "";
char gChatId[32] = "";

void copyJsonString(const String &json, const char *key, char *dest, size_t size) {
  const String token = String('"') + key + "\"";
  const int keyPos = json.indexOf(token);
  if (keyPos < 0) return;
  const int colon = json.indexOf(':', keyPos + token.length());
  const int firstQuote = json.indexOf('"', colon + 1);
  const int secondQuote = json.indexOf('"', firstQuote + 1);
  if (colon < 0 || firstQuote < 0 || secondQuote < 0) return;
  json.substring(firstQuote + 1, secondQuote).toCharArray(dest, size);
}

void loadTelegramConfig() {
  gBotToken[0] = '\0';
  gChatId[0] = '\0';
  File file = LittleFS.open("/machine.json", "r");
  if (!file) return;
  const String json = file.readString();
  file.close();
  copyJsonString(json, "telegram_bot_token", gBotToken, sizeof(gBotToken));
  copyJsonString(json, "telegram_chat_id", gChatId, sizeof(gChatId));
}

String urlEncode(const String &value) {
  String encoded;
  encoded.reserve(value.length() * 2);
  const char *hex = "0123456789ABCDEF";
  for (size_t i = 0; i < value.length(); ++i) {
    const uint8_t c = static_cast<uint8_t>(value[i]);
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
        (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~') {
      encoded += static_cast<char>(c);
    } else {
      encoded += '%';
      encoded += hex[(c >> 4) & 0x0F];
      encoded += hex[c & 0x0F];
    }
  }
  return encoded;
}

bool request(const String &url, String *response = nullptr) {
  if (!WiFiManager::connected()) return false;
  BearSSL::WiFiClientSecure client;
  client.setInsecure();
  client.setTimeout(10000);
  HTTPClient http;
  if (!http.begin(client, url)) {
    Logger::warn(F("[TELEGRAM] HTTP begin failed"));
    return false;
  }
  http.setTimeout(10000);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  const int code = http.GET();
  const String body = code > 0 ? http.getString() : String();
  http.end();
  if (response) *response = body;

  Logger::info(String(F("[TELEGRAM] HTTP status: ")) + code);
  if (body.length()) {
    String preview = body;
    if (preview.length() > 300) preview = preview.substring(0, 300) + F("...");
    Logger::info(String(F("[TELEGRAM] Response: ")) + preview);
  }

  return code >= 200 && code < 300 && body.indexOf(F("\"ok\":true")) >= 0;
}

String apiUrl(const __FlashStringHelper *method) {
  String url = F("https://api.telegram.org/bot");
  url += gBotToken;
  url += '/';
  url += method;
  return url;
}

bool verifyBot() {
  if (!TelegramClient::configured() || !WiFiManager::connected()) return false;
  String body;
  const bool ok = request(apiUrl(F("getMe")), &body);
  gVerificationAttempted = true;
  gLastVerifyAttemptMs = millis();
  gVerified = ok;
  if (ok) Logger::info(F("[TELEGRAM] Bot connected"));
  else Logger::warn(F("[TELEGRAM] Bot verification failed"));
  return ok;
}

bool sendText(const String &message) {
  if (!TelegramClient::configured() || !WiFiManager::connected()) return false;
  gLastMessageAttemptMs = millis();
  String url = apiUrl(F("sendMessage"));
  url += F("?chat_id=");
  url += urlEncode(gChatId);
  url += F("&text=");
  url += urlEncode(message);
  const bool ok = request(url);
  if (ok) {
    ++gSuccess;
    Logger::info(F("[TELEGRAM] Message sent"));
  } else {
    ++gFailure;
    Logger::warn(F("[TELEGRAM] Message failed; retry in 30 seconds"));
  }
  return ok;
}

bool sendDocument(const String &path) {
  if (!TelegramClient::configured() || !WiFiManager::connected()) return false;
  gLastMessageAttemptMs = millis();
  File file = LittleFS.open(path, "r");
  if (!file) {
    Logger::warn(F("[TELEGRAM] CSV file not found"));
    return false;
  }

  const String boundary = F("----AFMSBoundary7MA4YWxkTrZu0gW");
  String filename = path;
  const int slash = filename.lastIndexOf('/');
  if (slash >= 0) filename = filename.substring(slash + 1);

  String prefix;
  prefix.reserve(256);
  prefix += F("--"); prefix += boundary; prefix += F("\r\n");
  prefix += F("Content-Disposition: form-data; name=\"chat_id\"\r\n\r\n");
  prefix += gChatId;
  prefix += F("\r\n--"); prefix += boundary; prefix += F("\r\n");
  prefix += F("Content-Disposition: form-data; name=\"caption\"\r\n\r\n");
  prefix += F("AFMS monthly shift report - "); prefix += Config::machineName();
  prefix += F("\r\n--"); prefix += boundary; prefix += F("\r\n");
  prefix += F("Content-Disposition: form-data; name=\"document\"; filename=\"");
  prefix += filename;
  prefix += F("\"\r\nContent-Type: text/csv\r\n\r\n");

  String suffix;
  suffix += F("\r\n--"); suffix += boundary; suffix += F("--\r\n");
  const size_t contentLength = prefix.length() + file.size() + suffix.length();

  BearSSL::WiFiClientSecure client;
  client.setInsecure();
  client.setTimeout(15000);
  if (!client.connect("api.telegram.org", 443)) {
    file.close();
    Logger::warn(F("[TELEGRAM] Document connection failed"));
    return false;
  }

  client.print(F("POST /bot")); client.print(gBotToken); client.println(F("/sendDocument HTTP/1.1"));
  client.println(F("Host: api.telegram.org"));
  client.print(F("Content-Type: multipart/form-data; boundary=")); client.println(boundary);
  client.print(F("Content-Length: ")); client.println(contentLength);
  client.println(F("Connection: close"));
  client.println();
  client.print(prefix);

  uint8_t buffer[512];
  while (file.available()) {
    const size_t count = file.read(buffer, sizeof(buffer));
    if (count) client.write(buffer, count);
    yield();
  }
  file.close();
  client.print(suffix);

  const uint32_t started = millis();
  while (!client.available() && client.connected() && millis() - started < 15000UL) yield();
  String response;
  while (client.available()) {
    response += static_cast<char>(client.read());
    if (response.length() > 1024) response.remove(0, 256);
  }
  client.stop();

  if (response.length()) {
    String preview = response;
    if (preview.length() > 400) preview = preview.substring(preview.length() - 400);
    Logger::info(String(F("[TELEGRAM] Document response: ")) + preview);
  }

  const bool ok = response.indexOf(F(" 200 ")) >= 0 && response.indexOf(F("\"ok\":true")) >= 0;
  if (ok) {
    ++gSuccess;
    Logger::info(String(F("[TELEGRAM] Monthly CSV sent: ")) + filename);
  } else {
    ++gFailure;
    Logger::warn(F("[TELEGRAM] Monthly CSV send failed; retry in 30 seconds"));
  }
  return ok;
}
}

void TelegramClient::begin() {
  gVerified = false;
  gVerificationAttempted = false;
  gReadyMessageSent = false;
  gLastMessageAttemptMs = 0;
  gPendingLossCode = 0;
  gPendingLossDurationSeconds = 0;
  gPendingMonthlyReport = "";
  loadTelegramConfig();
  if (!configured()) Logger::warn(F("[TELEGRAM] Bot token or chat ID missing"));
  else Logger::info(F("[TELEGRAM] Configuration loaded"));
}

void TelegramClient::update() {
  if (!configured() || !WiFiManager::connected()) return;

  if (!gVerified) {
    if (!gVerificationAttempted || millis() - gLastVerifyAttemptMs >= kVerifyRetryMs) verifyBot();
    if (!gVerified) return;
  }

  if (gLastMessageAttemptMs && millis() - gLastMessageAttemptMs < kMessageRetryMs) return;

  if (!gReadyMessageSent) {
    gReadyMessageSent = sendMachineReady();
    return;
  }

  if (gPendingLossCode >= 1 && gPendingLossCode <= 16) {
    if (sendLoss(gPendingLossCode, gPendingLossDurationSeconds)) {
      gPendingLossCode = 0;
      gPendingLossDurationSeconds = 0;
    }
    return;
  }

  if (!gPendingMonthlyReport.length()) {
    ShiftCsvManager::consumeDailyReportReady(gPendingMonthlyReport);
  }
  if (gPendingMonthlyReport.length() && sendDocument(gPendingMonthlyReport)) {
    gPendingMonthlyReport = "";
  }
}

bool TelegramClient::configured() { return gBotToken[0] && gChatId[0]; }
bool TelegramClient::connected() { return gVerified; }

bool TelegramClient::sendMachineReady() {
  String message = F("AFMS machine ready\nMachine: ");
  message += Config::machineName();
  message += F("\nMachine ID: ");
  message += Config::machineId();
  message += F("\nTime: ");
  message += TimeManager::iso8601();
  return sendText(message);
}

bool TelegramClient::sendLoss(uint16_t lossCode, uint32_t durationSeconds) {
  String message = F("AFMS loss recorded\nMachine: ");
  message += Config::machineName();
  message += F("\nLoss code: ");
  message += lossCode;
  message += F("\nDuration: ");
  message += durationSeconds;
  message += F(" sec\nTime: ");
  message += TimeManager::iso8601();
  return sendText(message);
}

void TelegramClient::queueLoss(uint16_t lossCode, uint32_t durationSeconds) {
  if (lossCode < 1 || lossCode > 16) return;
  gPendingLossCode = lossCode;
  gPendingLossDurationSeconds = durationSeconds;
}

uint32_t TelegramClient::successCount() { return gSuccess; }
uint32_t TelegramClient::failureCount() { return gFailure; }
