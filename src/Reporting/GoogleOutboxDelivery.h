#pragma once

#include "ReportOutboxManager.h"

namespace GoogleOutboxDelivery {

using UploadCallback = bool (*)(const String &payload);

struct State {
  uint32_t successCount = 0;
  uint32_t failureCount = 0;
  uint32_t nextAttemptMs = 0;
  uint8_t failureStreak = 0;
  bool connected = false;
};

inline State &state() {
  static State value;
  return value;
}

inline void begin() { state() = State(); }

inline uint32_t retryDelayMs(uint8_t failureStreak) {
  constexpr uint32_t kBaseMs = 30000UL;
  constexpr uint32_t kMaximumMs = 900000UL;
  const uint8_t exponent = failureStreak > 5 ? 5 : failureStreak;
  const uint32_t calculated = kBaseMs << exponent;
  return calculated > kMaximumMs ? kMaximumMs : calculated;
}

inline bool due(uint32_t nowMs) {
  return state().nextAttemptMs == 0 ||
         static_cast<int32_t>(nowMs - state().nextAttemptMs) >= 0;
}

inline bool update(bool wifiConnected, UploadCallback upload) {
  State &delivery = state();
  if (!wifiConnected || upload == nullptr) {
    delivery.connected = false;
    return false;
  }

  const uint32_t nowMs = millis();
  if (!due(nowMs)) return false;

  ReportOutboxManager::ReportRecord report;
  if (!ReportOutboxManager::nextPending(ReportOutboxManager::Destination::Google, report)) {
    delivery.failureStreak = 0;
    delivery.nextAttemptMs = 0;
    return false;
  }

  if (!upload(report.payload)) {
    delivery.connected = false;
    if (delivery.failureStreak < 255) ++delivery.failureStreak;
    ++delivery.failureCount;
    delivery.nextAttemptMs = nowMs + retryDelayMs(delivery.failureStreak);
    return false;
  }

  if (!ReportOutboxManager::acknowledge(report.storagePath,
                                        ReportOutboxManager::Destination::Google)) {
    delivery.connected = false;
    if (delivery.failureStreak < 255) ++delivery.failureStreak;
    ++delivery.failureCount;
    delivery.nextAttemptMs = nowMs + retryDelayMs(delivery.failureStreak);
    return false;
  }

  ReportOutboxManager::removeIfCompleted(report.storagePath);
  delivery.connected = true;
  delivery.failureStreak = 0;
  delivery.nextAttemptMs = nowMs + 1000UL;
  ++delivery.successCount;
  return true;
}

inline bool connected() { return state().connected; }
inline uint32_t successCount() { return state().successCount; }
inline uint32_t failureCount() { return state().failureCount; }

}  // namespace GoogleOutboxDelivery
