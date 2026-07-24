#include "OfflineQueue.h"
#include <LittleFS.h>
#include <ArduinoJson.h>

namespace {
constexpr uint16_t kMaxRecords = 250;
constexpr size_t kMaxFileBytes = 96U * 1024U;
uint16_t gCounts[2] = {0, 0};
uint32_t gDropped[2] = {0, 0};
uint32_t gLegacySequence = 0;

uint8_t indexOf(OfflineQueue::Destination destination) {
  return destination == OfflineQueue::Destination::AfmsWeb ? 0 : 1;
}
const char *pathOf(OfflineQueue::Destination destination) {
  return destination == OfflineQueue::Destination::AfmsWeb ? "/queue-afms.ndjson" : "/queue-google.ndjson";
}
const char *tempOf(OfflineQueue::Destination destination) {
  return destination == OfflineQueue::Destination::AfmsWeb ? "/queue-afms.tmp" : "/queue-google.tmp";
}

uint16_t recount(const char *path) {
  File file = LittleFS.open(path, "r");
  if (!file) return 0;
  uint16_t lines = 0;
  while (file.available()) {
    const String line = file.readStringUntil('\n');
    if (line.length()) ++lines;
  }
  file.close();
  return lines;
}

size_t fileBytes(OfflineQueue::Destination destination) {
  File file = LittleFS.open(pathOf(destination), "r");
  const size_t bytes = file ? file.size() : 0;
  if (file) file.close();
  return bytes;
}

bool removeFirst(OfflineQueue::Destination destination) {
  const char *path = pathOf(destination);
  const char *tempPath = tempOf(destination);
  File source = LittleFS.open(path, "r");
  if (!source || !source.available()) {
    if (source) source.close();
    return false;
  }
  source.readStringUntil('\n');
  File temp = LittleFS.open(tempPath, "w");
  if (!temp) { source.close(); return false; }
  while (source.available()) temp.write(source.read());
  source.close();
  temp.flush();
  temp.close();
  LittleFS.remove(path);
  if (!LittleFS.rename(tempPath, path)) return false;
  const uint8_t index = indexOf(destination);
  if (gCounts[index]) --gCounts[index];
  return true;
}

String legacyEventId() {
  ++gLegacySequence;
  String id = F("google-");
  id += ESP.getChipId();
  id += '-';
  id += millis();
  id += '-';
  id += gLegacySequence;
  return id;
}
}

bool OfflineQueue::begin() {
  gCounts[0] = recount(pathOf(Destination::AfmsWeb));
  gCounts[1] = recount(pathOf(Destination::GoogleSheets));
  return true;
}

bool OfflineQueue::enqueue(Destination destination, const String &eventId, const String &payload) {
  if (!eventId.length() || !payload.length()) return false;
  const uint8_t index = indexOf(destination);
  while (gCounts[index] >= kMaxRecords || fileBytes(destination) >= kMaxFileBytes) {
    if (!removeFirst(destination)) break;
    ++gDropped[index];
  }

  DynamicJsonDocument document(payload.length() + eventId.length() + 192);
  document["eventId"] = eventId;
  document["queuedAtMs"] = millis();
  document["payload"] = serialized(payload);
  String record;
  serializeJson(document, record);

  File file = LittleFS.open(pathOf(destination), "a");
  if (!file) return false;
  const bool ok = file.println(record) > 0;
  file.flush();
  file.close();
  if (ok && gCounts[index] < 65535U) ++gCounts[index];
  return ok;
}

bool OfflineQueue::peek(Destination destination, String &eventId, String &payload) {
  File file = LittleFS.open(pathOf(destination), "r");
  if (!file || !file.available()) { if (file) file.close(); return false; }
  const String record = file.readStringUntil('\n');
  file.close();
  DynamicJsonDocument document(record.length() + 256);
  if (deserializeJson(document, record)) return false;
  eventId = document["eventId"] | "";
  if (document["payload"].is<JsonObject>() || document["payload"].is<JsonArray>()) serializeJson(document["payload"], payload);
  else payload = document["payload"] | "";
  return eventId.length() && payload.length();
}

bool OfflineQueue::removeHead(Destination destination) { return removeFirst(destination); }
uint16_t OfflineQueue::count(Destination destination) { return gCounts[indexOf(destination)]; }
uint16_t OfflineQueue::count() {
  const uint32_t total = static_cast<uint32_t>(gCounts[0]) + static_cast<uint32_t>(gCounts[1]);
  return total > 65535U ? 65535U : static_cast<uint16_t>(total);
}
uint32_t OfflineQueue::droppedCount(Destination destination) { return gDropped[indexOf(destination)]; }
void OfflineQueue::clear(Destination destination) {
  LittleFS.remove(pathOf(destination));
  LittleFS.remove(tempOf(destination));
  gCounts[indexOf(destination)] = 0;
}

bool OfflineQueue::push(const String &payload) {
  return enqueue(Destination::GoogleSheets, legacyEventId(), payload);
}

bool OfflineQueue::peek(String &payload) {
  String eventId;
  return peek(Destination::GoogleSheets, eventId, payload);
}

bool OfflineQueue::pop() {
  return removeHead(Destination::GoogleSheets);
}
