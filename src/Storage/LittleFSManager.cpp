#include "LittleFSManager.h"
#include "../Core/Logger.h"
#include <LittleFS.h>

namespace {
bool gReady = false;

String formatBytes(size_t bytes) {
  if (bytes < 1024U) return String(bytes) + F(" B");
  if (bytes < 1024U * 1024U) return String(bytes / 1024.0F, 1) + F(" KB");
  return String(bytes / (1024.0F * 1024.0F), 2) + F(" MB");
}

void printInventory() {
  Logger::info(F("[LFS] File inventory"));

  Dir directory = LittleFS.openDir("/");
  uint16_t fileCount = 0;

  while (directory.next()) {
    ++fileCount;
    Logger::info(String(F("[LFS] ")) + directory.fileName() + F(" | ") + formatBytes(directory.fileSize()));
  }

  FSInfo info;
  if (LittleFS.info(info)) {
    Logger::info(String(F("[LFS] Total files: ")) + fileCount);
    Logger::info(String(F("[LFS] Used: ")) + formatBytes(info.usedBytes));
    Logger::info(String(F("[LFS] Free: ")) + formatBytes(info.totalBytes - info.usedBytes));
    Logger::info(String(F("[LFS] Capacity: ")) + formatBytes(info.totalBytes));
  } else {
    Logger::warn(F("[LFS] Filesystem statistics unavailable"));
  }

  if (!LittleFS.exists("/machine.json")) {
    Logger::error(F("[LFS] REQUIRED FILE MISSING: /machine.json"));
  } else {
    Logger::info(F("[LFS] /machine.json present"));
  }
}
}

bool LittleFSManager::begin() {
  gReady = LittleFS.begin();
  if (gReady) {
    Logger::info(F("LittleFS mounted"));
    printInventory();
  }
  return gReady;
}

bool LittleFSManager::ready() { return gReady; }
