# Commit 0030S Performance Correction

## Changes

- Expanded production interrupt queue from 8 to 64 entries.
- Added dropped-pulse diagnostics.
- Added one-network-transaction-per-loop cooperative budget.
- Gave fresh AFMS status/loss telemetry priority over historical replay.
- Reduced foreground HTTP/TLS timeouts to 3 seconds (5 seconds for Telegram documents).
- Cached AFMS device credentials instead of reading JSON on every upload.
- Added maximum-loop-duration and deferred-network diagnostics.
- Updated diagnostics for device.json and server.json.

## Required validation

1. Compile with ESP8266 core 3.1.2.
2. Run production pulses for 10 minutes with Oracle disconnected.
3. Confirm `[PULSE] Dropped total: 0`.
4. Confirm machine/HMI state updates remain responsive during failed network requests.
5. Observe `[PERF] Max loop` once per minute.
6. Reconnect Oracle and confirm fresh telemetry is sent before queued replay.

## Acceptance

- No production pulse loss in the disconnection test.
- Normal HMI state response below 200 ms when no network transaction is active.
- Network stalls capped near configured 3-second timeout rather than 10-15 seconds.
