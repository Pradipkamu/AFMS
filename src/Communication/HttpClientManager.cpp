#include "HttpClientManager.h"
#include "WiFiManager.h"
#include "../Reporting/GoogleOutboxDelivery.h"
#include <ESP8266HTTPClient.h>
#include <WiFiClientSecureBearSSL.h>

namespace {
uint16_t gTimeoutMs = 10000;
uint32_t gFailures = 0;

void normalizeBenignGoogleResponse(String &body) {
  body.replace(F("\"error\":null"), F("\"error_cleared\":true"));
  body.replace(F("\"error\": null"), F("\"error_cleared\":true"));
  body.replace(F("\"error\":\"\""), F("\"error_cleared\":true"));
  body.replace(F("\"error\": \"\""), F("\"error_cleared\":true"));
}

HttpResult request(const char *url, const String *payload) {
  if (!url || !url[0] || !WiFiManager::connected()) {
    GoogleOutboxDelivery::setReachable(false);
    return {-1, String()};
  }

  BearSSL::WiFiClientSecure client;
  client.setInsecure();
  client.setTimeout(gTimeoutMs);

  HTTPClient http;
  if (!http.begin(client, url)) {
    ++gFailures;
    GoogleOutboxDelivery::setReachable(false);
    return {-2, String()};
  }

  http.setTimeout(gTimeoutMs);
  http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
  const char *headerKeys[] = {"Location"};
  http.collectHeaders(headerKeys, 1);

  int code;
  if (payload) {
    http.addHeader(F("Content-Type"), F("application/json"));
    code = http.POST(*payload);
  } else {
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    code = http.GET();
  }

  String body = code > 0 ? http.getString() : String();

  if (payload && (code == 301 || code == 302 || code == 303 || code == 307 || code == 308)) {
    const String location = http.header("Location");
    if (location.indexOf(F("script.googleusercontent.com")) >= 0 ||
        location.indexOf(F("googleusercontent.com")) >= 0) {
      code = 204;
      body = F("Google Apps Script redirect accepted; POST already processed");
    }
  }

  const bool reachable = code >= 200 && code < 300;
  GoogleOutboxDelivery::setReachable(reachable);
  if (reachable) normalizeBenignGoogleResponse(body);
  http.end();

  if (!reachable) ++gFailures;
  return {code, body};
}
}

void HttpClientManager::begin(uint16_t timeoutMs) { gTimeoutMs = timeoutMs; }
HttpResult HttpClientManager::postJson(const char *url, const String &payload) { return request(url, &payload); }
HttpResult HttpClientManager::get(const char *url) { return request(url, nullptr); }
uint32_t HttpClientManager::failureCount() { return gFailures; }
