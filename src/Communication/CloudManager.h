#pragma once

namespace CloudManager {
void begin();
void update();
void queueStatusNow();
uint32_t uploadSuccessCount();
uint32_t uploadFailureCount();
}
