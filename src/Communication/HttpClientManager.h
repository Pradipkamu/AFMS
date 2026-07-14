#pragma once
#include <Arduino.h>

struct HttpResult {
  int code;
  String body;
  bool success() const { return code >= 200 && code < 300; }
};

namespace HttpClientManager {
void begin(uint16_t timeoutMs = 10000);
HttpResult postJson(const char *url, const String &payload);
HttpResult get(const char *url);
uint32_t failureCount();
}
