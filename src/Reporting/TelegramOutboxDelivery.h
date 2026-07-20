#pragma once

#include "ReportOutboxManager.h"

namespace TelegramOutboxDelivery {

using MessageCallback = bool (*)(const String &message);
using DocumentCallback = bool (*)(const String &path, const String &caption);

struct State {
  uint32_t successCount = 0;
  uint32_t failureCount = 0;
  uint32_t nextAttemptMs = 0;
  bool verified = false;
};

inline State &state() {
  static State value;
  return value;
}

inline void begin() { state() = State(); }

inline void setVerified(bool verified) {
  state().verified = verified;
  if (!verified) state().nextAttemptMs = 0;
}

inline void onWifiDisconnected() { setVerified(false); }
inline void onWifiReconnected() { setVerified(false); }

inline bool update(bool wifiConnected,
                   MessageCallback sendMessage,
                   DocumentCallback sendDocument) {
  State &delivery = state();
  if (!wifiConnected || !delivery.verified) return false;

  const uint32_t nowMs = millis();
  if (delivery.nextAttemptMs != 0 &&
      static_cast<int32_t>(nowMs - delivery.nextAttemptMs) < 0) {
    return false;
  }

  ReportOutboxManager::ReportRecord report;
  if (!ReportOutboxManager::nextPending(ReportOutboxManager::Destination::Telegram,
                                        report)) {
    delivery.nextAttemptMs = 0;
    return false;
  }

  bool sent = false;
  if (report.documentPath.length()) {
    if (sendDocument != nullptr) sent = sendDocument(report.documentPath, report.payload);
  } else if (sendMessage != nullptr) {
    sent = sendMessage(report.payload);
  }

  if (!sent) {
    ++delivery.failureCount;
    delivery.nextAttemptMs = nowMs + 30000UL;
    return false;
  }

  if (!ReportOutboxManager::acknowledge(report.storagePath,
                                        ReportOutboxManager::Destination::Telegram)) {
    ++delivery.failureCount;
    delivery.nextAttemptMs = nowMs + 30000UL;
    return false;
  }

  ReportOutboxManager::removeIfCompleted(report.storagePath);
  ++delivery.successCount;
  delivery.nextAttemptMs = nowMs + 30000UL;
  return true;
}

inline bool verified() { return state().verified; }
inline uint32_t successCount() { return state().successCount; }
inline uint32_t failureCount() { return state().failureCount; }

}  // namespace TelegramOutboxDelivery
