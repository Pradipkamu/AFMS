#include "LossCatalog.h"
#include "Logger.h"
#include <LittleFS.h>

namespace {
constexpr uint16_t kMaxLossCode = 16;
String gNames[kMaxLossCode + 1];
bool gLoaded = false;

void setFallbackNames() {
  gNames[0] = F("Unknown loss");
  for (uint16_t code = 1; code <= kMaxLossCode; ++code) {
    gNames[code] = String(F("Loss ")) + code;
  }
}

bool parseName(const String &json, uint16_t code, String &name) {
  const String codeToken = String(F("\"code\""));
  int searchFrom = 0;
  while (true) {
    const int codePos = json.indexOf(codeToken, searchFrom);
    if (codePos < 0) return false;
    const int codeColon = json.indexOf(':', codePos + codeToken.length());
    if (codeColon < 0) return false;
    const int codeEnd = json.indexOf(',', codeColon + 1);
    if (codeEnd < 0) return false;
    const uint16_t parsedCode = static_cast<uint16_t>(json.substring(codeColon + 1, codeEnd).toInt());
    if (parsedCode == code) {
      const int namePos = json.indexOf(F("\"name\""), codeEnd);
      if (namePos < 0) return false;
      const int nameColon = json.indexOf(':', namePos + 6);
      const int firstQuote = json.indexOf('"', nameColon + 1);
      const int secondQuote = json.indexOf('"', firstQuote + 1);
      if (nameColon < 0 || firstQuote < 0 || secondQuote < 0) return false;
      name = json.substring(firstQuote + 1, secondQuote);
      name.trim();
      return name.length() > 0;
    }
    searchFrom = codeEnd + 1;
  }
}
}

void LossCatalog::begin() {
  setFallbackNames();
  gLoaded = false;

  File file = LittleFS.open("/losses.json", "r");
  if (!file) {
    Logger::warn(F("[LOSS] /losses.json missing; fallback names active"));
    return;
  }

  const String json = file.readString();
  file.close();
  if (!json.length()) {
    Logger::warn(F("[LOSS] /losses.json empty; fallback names active"));
    return;
  }

  uint16_t loadedCount = 0;
  for (uint16_t code = 1; code <= kMaxLossCode; ++code) {
    String parsed;
    if (parseName(json, code, parsed)) {
      gNames[code] = parsed;
      ++loadedCount;
    }
  }

  gLoaded = loadedCount > 0;
  Logger::info(String(F("[LOSS] Catalog loaded: ")) + loadedCount + F(" names"));
}

const String &LossCatalog::name(uint16_t lossCode) {
  if (lossCode <= kMaxLossCode) return gNames[lossCode];
  return gNames[0];
}

bool LossCatalog::loaded() { return gLoaded; }
