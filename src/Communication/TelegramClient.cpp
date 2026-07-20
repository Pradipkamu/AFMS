#include "TelegramClient.h"
#include "WiFiManager.h"
#include "TimeManager.h"
#include "../Core/Config.h"
#include "../Core/LossCatalog.h"
#include "../Core/Logger.h"
#include "../Reporting/ReportOutboxManager.h"
#include "../Reporting/TelegramOutboxDelivery.h"
#include <ArduinoJson.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClientSecureBearSSL.h>
#include <LittleFS.h>

namespace {
bool gVerified = false;
bool gVerificationAttempted = false;
uint32_t gTransportSuccess = 0;
uint32_t gTransportFailure = 0;
uint32_t gEnqueueFailure = 0;
uint32_t gLastVerifyAttemptMs = 0;
uint32_t gLossSequence = 0;
constexpr uint32_t kVerifyRetryMs = 60000UL;
char gBotToken[96] = "";
char gChatId[32] = "";

String firstConfigValue(const JsonDocument &document,
                        const char *primary,
                        const char *alias1 = nullptr,
                        const char *alias2 = nullptr) {
  const char *keys[] = {primary, alias1, alias2};
  for (uint8_t index = 0; index < 3; ++index) {
    const char *key = keys[index];
    if (!key || !document.containsKey(key) || document[key].isNull()) continue;
    String value = document[key].as<String>();
    value.trim();
    if (value.length()) return value;
  }
  return String();
}

void loadTelegramConfig() {
  gBotToken[0] = '\0';
  gChatId[0] = '\0';

  File file = LittleFS.open("/machine.json", "r");
  if (!file) {
    Logger::warn(F("[TELEGRAM] /machine.json could not be opened"));
    return;
  }

  DynamicJsonDocument document(6144);
  const DeserializationError error = deserializeJson(document, file);
  file.close();
  if (error) {
    Logger::warn(String(F("[TELEGRAM] Configuration JSON invalid: ")) + error.c_str());
    return;
  }

  const String token = firstConfigValue(document,
                                        "telegram_bot_token",
                                        "telegramBotToken",
                                        "bot_token");
  const String chatId = firstConfigValue(document,
                                         "telegram_chat_id",
                                         "telegramChatId",
                                         "chat_id");
  token.toCharArray(gBotToken, sizeof(gBotToken));
  chatId.toCharArray(gChatId, sizeof(gChatId));

  Logger::info(String(F("[TELEGRAM] Config token=")) +
               (gBotToken[0] ? F("present") : F("missing")) +
               F(" chat_id=") + (gChatId[0] ? gChatId : "missing"));
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
    if (preview.length() > 400) preview = preview.substring(0, 400) + F("...");
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
  TelegramOutboxDelivery::setVerified(ok);
  if (ok) Logger::info(F("[TELEGRAM] Bot connected and delivery enabled"));
  else Logger::warn(F("[TELEGRAM] Bot verification failed"));
  return ok;
}

bool sendTextTransport(const String &message) {
  if (!TelegramClient::configured() || !WiFiManager::connected()) {
    Logger::warn(F("[TELEGRAM] Text send blocked: configuration or WiFi unavailable"));
    return false;
  }
  String url = apiUrl(F("sendMessage"));
  url += F("?chat_id=");
  url += urlEncode(gChatId);
  url += F("&text=");
  url += urlEncode(message);
  Logger::info(String(F("[TELEGRAM] Sending queued text to chat ")) + gChatId);
  const bool ok = request(url);
  if (ok) {
    ++gTransportSuccess;
    Logger::info(F("[TELEGRAM] Text message accepted by Telegram"));
  } else {
    ++gTransportFailure;
    Logger::warn(F("[TELEGRAM] Text message failed; retained for retry"));
  }
  return ok;
}

bool sendDocumentTransport(const String &path, const String &caption) {
  if (!TelegramClient::configured() || !WiFiManager::connected()) return false;
  File file = LittleFS.open(path, "r");
  if (!file) {
    Logger::warn(String(F("[TELEGRAM] Document not found: ")) + path);
    ++gTransportFailure;
    return false;
  }

  const String boundary = F("----AFMSBoundary7MA4YWxkTrZu0gW");
  String filename = path;
  const int slash = filename.lastIndexOf('/');
  if (slash >= 0) filename = filename.substring(slash + 1);

  String prefix;
  prefix.reserve(320);
  prefix += F("--"); prefix += boundary; prefix += F("\r\n");
  prefix += F("Content-Disposition: form-data; name=\"chat_id\"\r\n\r\n");
  prefix += gChatId;
  prefix += F("\r\n--"); prefix += boundary; prefix += F("\r\n");
  prefix += F("Content-Disposition: form-data; name=\"caption\"\r\n\r\n");
  prefix += caption.length() ? caption : String(F("AFMS monthly report - ")) + Config::machineName();
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
    ++gTransportFailure;
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

  const bool ok = response.indexOf(F(" 200 ")) >= 0 &&
                  response.indexOf(F("\"ok\":true")) >= 0;
  if (ok) {
    ++gTransportSuccess;
    Logger::info(String(F("[TELEGRAM] Document sent: ")) + filename);
  } else {
    ++gTransportFailure;
    Logger::warn(F("[TELEGRAM] Document send failed"));
  }
  return ok;
}

String lossMessage(uint16_t lossCode, uint32_t durationSeconds) {
  String message = F("AFMS loss recorded\nMachine: ");
  message += Config::machineName();
  message += F("\nLoss: ");
  message += LossCatalog::name(lossCode);
  message += F("\nLoss code: ");
  message += lossCode;
  message += F("\nDuration: ");
  message += durationSeconds;
  message += F(" sec\nTime: ");
  message += TimeManager::iso8601();
  return message;
}
}

void TelegramClient::begin() {
  gVerified = false;
  gVerificationAttempted = false;
  gLastVerifyAttemptMs = 0;
  gLossSequence = 0;
  loadTelegramConfig();
  TelegramOutboxDelivery::begin();
  if (!configured()) Logger::warn(F("[TELEGRAM] Bot token or chat ID missing"));
  else Logger::info(F("[TELEGRAM] Configuration loaded"));
}

void TelegramClient::update() {
  if (!configured() || !WiFiManager::connected()) {
    gVerified = false;
    TelegramOutboxDelivery::onWifiDisconnected();
    return;
  }

  if (!gVerified) {
    if (!gVerificationAttempted || millis() - gLastVerifyAttemptMs >= kVerifyRetryMs) verifyBot();
    if (!gVerified) return;
  }

  TelegramOutboxDelivery::update(true, sendTextTransport, sendDocumentTransport);
}

bool TelegramClient::configured() { return gBotToken[0] && gChatId[0]; }
bool TelegramClient::connected() { return gVerified && TelegramOutboxDelivery::verified(); }

bool TelegramClient::sendMachineReady() {
  Logger::warn(F("[TELEGRAM] Machine Ready reporting is disabled in AFMS v3.1"));
  return false;
}

bool TelegramClient::sendLoss(uint16_t lossCode, uint32_t durationSeconds) {
  if (lossCode < 1 || lossCode > 16) return false;
  return sendTextTransport(lossMessage(lossCode, durationSeconds));
}

void TelegramClient::queueLoss(uint16_t lossCode, uint32_t durationSeconds) {
  if (lossCode < 1 || lossCode > 16) return;
  const time_t now = TimeManager::now();
  const uint32_t epoch = now > 0 ? static_cast<uint32_t>(now) : 0;
  ReportOutboxManager::ReportRecord report;
  report.type = ReportOutboxManager::ReportType::LossEvent;
  report.createdEpoch = epoch;
  report.priority = ReportOutboxManager::defaultPriority(report.type);
  report.googleRequired = false;
  report.telegramRequired = true;
  report.payload = lossMessage(lossCode, durationSeconds);
  report.reportId = F("TG-LOSS-");
  report.reportId += epoch;
  report.reportId += '-';
  report.reportId += lossCode;
  report.reportId += '-';
  report.reportId += durationSeconds;
  report.reportId += '-';
  report.reportId += ++gLossSequence;
  if (!ReportOutboxManager::enqueue(report)) {
    ++gEnqueueFailure;
    Logger::error(String(F("[TELEGRAM] Failed to persist loss notification: ")) + report.reportId);
    return;
  }

  Logger::info(String(F("[TELEGRAM] Loss queued asynchronously: ")) + report.reportId +
               F(" pending=") +
               ReportOutboxManager::pendingCount(ReportOutboxManager::Destination::Telegram));
}

uint32_t TelegramClient::successCount() {
  return TelegramOutboxDelivery::successCount();
}
uint32_t TelegramClient::failureCount() {
  return TelegramOutboxDelivery::failureCount() + gTransportFailure + gEnqueueFailure;
}
