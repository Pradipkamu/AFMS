#include "WebManager.h"
#include "../Core/Config.h"
#include "../Core/Logger.h"
#include "../Core/SystemHealth.h"
#include "../Core/Version.h"
#include "../Machine/MachineEngine.h"
#include <ESP8266WebServer.h>
#include <LittleFS.h>

namespace {
ESP8266WebServer gServer(80);
bool gRestartPending = false;
uint32_t gRestartAtMs = 0;

String jsonEscape(const char *value) {
  String out;
  if (!value) return out;
  while (*value) {
    if (*value == '"' || *value == '\\') out += '\\';
    out += *value++;
  }
  return out;
}

String statusJson() {
  const MachineSnapshot m = MachineEngine::snapshot();
  String out;
  out.reserve(512);
  out += F("{\"firmware\":\""); out += AFMS_VERSION;
  out += F("\",\"machine_id\":\""); out += jsonEscape(Config::machineId());
  out += F("\",\"machine_name\":\""); out += jsonEscape(Config::machineName());
  out += F("\",\"state\":"); out += static_cast<uint8_t>(m.state);
  out += F(",\"total\":"); out += m.totalParts;
  out += F(",\"reject\":"); out += m.rejectParts;
  out += F(",\"good\":"); out += m.goodParts;
  out += F(",\"target\":"); out += m.targetQuantity;
  out += F(",\"idle_seconds\":"); out += m.idleSeconds;
  out += F(",\"run_seconds\":"); out += m.runSeconds;
  out += F(",\"downtime_seconds\":"); out += m.downtimeSeconds;
  out += F(",\"oee_permille\":"); out += m.oeePermille;
  out += F(",\"alarm\":"); out += m.alarmActive ? F("true") : F("false");
  out += F(",\"health\":"); out += SystemHealth::json();
  out += '}';
  return out;
}

String configJson() {
  String out;
  out.reserve(384);
  out += F("{\"machine_id\":\""); out += jsonEscape(Config::machineId());
  out += F("\",\"machine_name\":\""); out += jsonEscape(Config::machineName());
  out += F("\",\"wifi_ssid\":\""); out += jsonEscape(Config::wifiSsid());
  out += F("\",\"google_web_app_url\":\""); out += jsonEscape(Config::googleWebAppUrl());
  out += F("\"}");
  return out;
}

void handleRoot() {
  String html;
  html.reserve(1800);
  html += F("<!doctype html><html><head><meta name='viewport' content='width=device-width,initial-scale=1'>");
  html += F("<title>AFMS</title><style>body{font-family:Arial;max-width:760px;margin:24px auto;padding:0 14px}input{width:100%;padding:9px;margin:5px 0 12px;box-sizing:border-box}button{padding:10px 18px}pre{background:#eee;padding:12px;overflow:auto}</style></head><body>");
  html += F("<h1>AI Factory Monitoring System</h1><p>Firmware "); html += AFMS_VERSION; html += F("</p>");
  html += F("<h2>Configuration</h2><form method='POST' action='/api/config'>");
  html += F("Machine ID<input name='machine_id' value='"); html += Config::machineId(); html += F("'>");
  html += F("Machine Name<input name='machine_name' value='"); html += Config::machineName(); html += F("'>");
  html += F("Wi-Fi SSID<input name='wifi_ssid' value='"); html += Config::wifiSsid(); html += F("'>");
  html += F("Wi-Fi Password<input type='password' name='wifi_password' placeholder='Leave blank to keep current password'>");
  html += F("Google Web App URL<input name='google_web_app_url' value='"); html += Config::googleWebAppUrl(); html += F("'>");
  html += F("<button type='submit'>Save and Restart</button></form>");
  html += F("<h2>Diagnostics</h2><p><a href='/api/status'>Live status JSON</a> | <a href='/api/config/download'>Download configuration</a></p>");
  html += F("<form method='POST' action='/api/reboot'><button type='submit'>Restart ESP</button></form></body></html>");
  gServer.send(200, F("text/html"), html);
}

void handleConfigPost() {
  const String password = gServer.arg("wifi_password").length() ? gServer.arg("wifi_password") : String(Config::wifiPassword());
  const bool saved = Config::update(gServer.arg("machine_id"),
                                    gServer.arg("machine_name"),
                                    gServer.arg("wifi_ssid"),
                                    password,
                                    gServer.arg("google_web_app_url"));
  if (!saved) {
    gServer.send(400, F("application/json"), F("{\"ok\":false,\"error\":\"invalid configuration\"}"));
    return;
  }
  gServer.send(200, F("text/html"), F("<html><body><h2>Configuration saved. Restarting...</h2></body></html>"));
  gRestartPending = true;
  gRestartAtMs = millis() + 1500UL;
}

void handleConfigDownload() {
  File file = LittleFS.open("/machine.json", "r");
  if (!file) {
    gServer.send(404, F("text/plain"), F("Configuration not found"));
    return;
  }
  gServer.streamFile(file, F("application/json"));
  file.close();
}
}

void WebManager::begin() {
  gServer.on("/", HTTP_GET, handleRoot);
  gServer.on("/api/status", HTTP_GET, []() { gServer.send(200, F("application/json"), statusJson()); });
  gServer.on("/api/config", HTTP_GET, []() { gServer.send(200, F("application/json"), configJson()); });
  gServer.on("/api/config", HTTP_POST, handleConfigPost);
  gServer.on("/api/config/download", HTTP_GET, handleConfigDownload);
  gServer.on("/api/reboot", HTTP_POST, []() {
    gServer.send(200, F("application/json"), F("{\"ok\":true}"));
    gRestartPending = true;
    gRestartAtMs = millis() + 1000UL;
  });
  gServer.onNotFound([]() { gServer.send(404, F("application/json"), F("{\"error\":\"not found\"}")); });
  gServer.begin();
  Logger::info(F("Web configuration server ready on port 80"));
}

void WebManager::update() {
  gServer.handleClient();
  if (gRestartPending && static_cast<int32_t>(millis() - gRestartAtMs) >= 0) ESP.restart();
}
