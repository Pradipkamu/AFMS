#include "HttpClientManager.h"
#include "WiFiManager.h"
#include <ESP8266HTTPClient.h>
#include <WiFiClientSecureBearSSL.h>

namespace {
uint16_t gTimeoutMs = 10000;
uint32_t gFailures = 0;

HttpResult request(const char *url, const String *payload) {
  if (!url || !url[0] || !WiFiManager::connected()) return {-1, String()};
  BearSSL::WiFiClientSecure client;
  client.setInsecure();
  client.setTimeout(gTimeoutMs);
  HTTPClient http;
  if (!http.begin(client, url)) {
    ++gFailures;
    return {-2, String()};
  }
  http.setTimeout(gTimeoutMs);
  int code;
  if (payload) {
    http.addHeader(F("Content-Type"), F("application/json"));
    code = http.POST(*payload);
  } else {
    code = http.GET();
  }
  const String body = code > 0 ? http.getString() : String();
  http.end();
  if (code < 200 || code >= 300) ++gFailures;
  return {code, body};
}
}

void HttpClientManager::begin(uint16_t timeoutMs) { gTimeoutMs = timeoutMs; }
HttpResult HttpClientManager::postJson(const char *url, const String &payload) { return request(url, &payload); }
HttpResult HttpClientManager::get(const char *url) { return request(url, nullptr); }
uint32_t HttpClientManager::failureCount() { return gFailures; }
