# Performance Fix Validation

Compile the performance branch, flash the ESP8266, and verify:

- `[PULSE] Dropped total: 0` after a 10-minute disconnected-server test.
- `[PERF] Max loop` is reported every minute.
- HMI production count and status remain responsive while uploads fail.
- Fresh status and loss telemetry is transmitted before offline queue replay.
