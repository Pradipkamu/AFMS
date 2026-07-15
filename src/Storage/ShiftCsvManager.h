#pragma once
#include <Arduino.h>
#include "../Machine/ShiftManager.h"
#include "../Machine/OEEManager.h"

namespace ShiftCsvManager {
bool begin();
bool appendShift(const ShiftSnapshot &shift,
                 const OEESnapshot &oee,
                 uint32_t endedAtEpoch);
const char *currentMonthlyPath();
bool consumeDailyReportReady(String &path);
}
