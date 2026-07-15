# AFMS Runtime State Persistence

AFMS saves production runtime state to LittleFS and restores it after an unexpected power loss.

## machine.json configuration

Add this object to the existing `data/machine.json` file:

```json
"runtime_state": {
  "enabled": true,
  "save_interval_seconds": 60,
  "restore_on_boot": true
}
```

The valid save interval is 10 to 3600 seconds. Missing or invalid values use 60 seconds.

## Saved file

Runtime state is stored atomically as:

```text
/runtime_state.json
```

AFMS first writes `/runtime_state.tmp`, closes and flushes it, then renames it to the final path. A checksum is included so corrupted state is rejected during boot.

## Data currently retained

- Total production count
- Total reject count
- Current shift ID
- Current shift production and reject baseline
- Operator ID
- Part number and part name
- Target quantity
- Shift start timestamp

Good quantity is recalculated from production minus reject.

## Power-loss test

1. Add the configuration to `machine.json` and upload LittleFS.
2. Upload the firmware.
3. Generate several production and reject pulses.
4. Wait more than the configured save interval.
5. Record HMI registers 40018-40023.
6. Remove ESP8266 power without a controlled shutdown.
7. Restore power.
8. Confirm production, reject and good counts return to the saved values.

With a 60-second interval, pulses received after the last completed save may be lost during an abrupt power failure. The full counter no longer resets to zero.
