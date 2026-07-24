#include "OfflineQueue.h"
#include "../Reporting/ReportOutboxManager.h"
#include <LittleFS.h>

namespace {
const char *kQueuePath = "/offline.queue";
uint16_t gLegacyCount = 0;

uint16_t recount() {
  File file = LittleFS.open(kQueuePath, "r");
  if (!file) return 0;
  uint16_t lines = 0;
  while (file.available()) {
    file.readStringUntil('\n');
    ++lines;
  }
  file.close();
  return lines;
}
}

bool OfflineQueue::begin() {
  gLegacyCount = recount();
  return true;
}

bool OfflineQueue::push(const String &record) {
  File file = LittleFS.open(kQueuePath, "a");
  if (!file) return false;
  const bool ok = file.println(record) > 0;
  file.close();
  if (ok && gLegacyCount < 65535U) ++gLegacyCount;
  return ok;
}

bool OfflineQueue::peek(String &record) {
  File file = LittleFS.open(kQueuePath, "r");
  if (!file || !file.available()) {
    if (file) file.close();
    return false;
  }
  record = file.readStringUntil('\n');
  file.close();
  return true;
}

bool OfflineQueue::pop() {
  File source = LittleFS.open(kQueuePath, "r");
  if (!source || !source.available()) {
    if (source) source.close();
    return false;
  }
  source.readStringUntil('\n');
  File temp = LittleFS.open("/offline.tmp", "w");
  if (!temp) {
    source.close();
    return false;
  }
  while (source.available()) temp.write(source.read());
  source.close();
  temp.close();
  LittleFS.remove(kQueuePath);
  if (!LittleFS.rename("/offline.tmp", kQueuePath)) return false;
  if (gLegacyCount > 0) --gLegacyCount;
  return true;
}

uint16_t OfflineQueue::count() {
  const uint16_t outbox = ReportOutboxManager::count();
  return outbox ? outbox : gLegacyCount;
}
