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

  // Google Apps Script commonly processes the POST and then returns a redirect
  // to script.googleusercontent.com for the response body. Automatically
  // following that redirect with the original POST can create a duplicate row.
  // Therefore POST redirects are not followed. A trusted Google response
  // redirect is treated as successful delivery after the original POST.
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

  http.end();

  if (code < 200 || code >= 300) ++gFailures;
  return {code, body};
}
}

void HttpClientManager::begin(uint16_t timeoutMs) { gTimeoutMs = timeoutMs; }
HttpResult HttpClientManager::postJson(const char *url, const String &payload) { return request(url, &payload); }
HttpResult HttpClientManager::get(const char *url) { return request(url, nullptr); }
uint32_t HttpClientManager::failureCount() { return gFailures; }
