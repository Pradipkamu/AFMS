#pragma once
#include <Arduino.h>
#include "../Machine/MachineEngine.h"
#include "../Machine/ShiftManager.h"
#include "../Machine/OEEManager.h"

namespace ReportingCsvManager {
bool begin();
bool appendLoss(uint32_t epoch, uint16_t lossCode, const char *lossName,
                uint32_t durationSeconds, const MachineSnapshot &machine,
                const ShiftSnapshot &shift);
bool appendHourly(uint32_t periodEndEpoch, const MachineSnapshot &machine,
                  const ShiftSnapshot &shift, const OEESnapshot &oee,
                  bool recovered);
bool writeStatistics(uint32_t epoch, const MachineSnapshot &machine,
                     const ShiftSnapshot &shift, const OEESnapshot &oee);
String lossPath(uint32_t epoch);
String hourlyPath(uint32_t epoch);
String statisticsPath(uint32_t epoch);
}
