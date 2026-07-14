#pragma once

namespace OtaManager {
void begin(const char *hostname);
void update();
bool active();
}
