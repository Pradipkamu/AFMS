#pragma once
#include <Arduino.h>

namespace Config {
bool load();
bool save();
bool update(const String &machineId,
            const String &machineName,
            const String &wifiSsid,
            const String &wifiPassword,
            const String &googleWebAppUrl);
const char *machineId();
const char *machineName();
const char *wifiSsid();
const char *wifiPassword();
const char *googleWebAppUrl();
const char *apiToken();
const char *telegramBotToken();
const char *telegramChatId();
}
