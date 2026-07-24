# Commit 0030S — Phases 3 to 5

## Phase 3 — Communication and runtime integration

The firmware must be tested on an ESP8266 using a non-production machine configuration before release.

### Startup and fallback

- LittleFS mounts successfully.
- Built-in/default configuration starts when no local configuration exists.
- Last-known-good `machine.json` loads after restart.
- Invalid downloaded configuration is rejected without replacing the working file.
- `configSync.enabled=false` stops remote polling while retaining the current configuration.

### AFMS Web path

- Device authenticates using its machine-specific API key.
- Periodic telemetry follows the configured interval.
- Status, loss-code and production-milestone changes trigger immediate telemetry.
- HTTP 200 and 201 are treated as success.
- Invalid or disabled device keys return failure without deleting queued records.
- Duplicate `eventId` responses are treated as successful delivery.

### Offline recovery

- Failed AFMS records are persisted in the AFMS queue.
- Failed Google Sheets records remain isolated in the Google queue.
- A controller restart preserves both queues.
- AFMS records replay oldest-first after reconnection.
- Successful replay removes only the delivered head record.
- Repeated failures use bounded retry backoff.

### Backend behavior

- Authenticated telemetry appears in the live dashboard.
- Telemetry updates OEE and reports.
- Breakdown, stopped and idle telemetry opens downtime as configured.
- Returning to running closes active downtime.
- Device `lastSeenAt` and firmware version update.

### Long-run test

Run the controller for at least eight hours while exercising Wi-Fi loss, Oracle restart and Google Sheets failure. Confirm no watchdog reset, heap collapse, queue corruption or blocked HMI communication.

## Phase 4 — Flash and RAM audit

GitHub Actions now exports the firmware binary, ELF file, compiler log and artifact-size report.

Release review must record:

- Compiled `.bin` size.
- Static RAM usage reported by the ESP8266 linker.
- IRAM usage.
- Free heap after startup.
- Lowest observed free heap during telemetry, TLS, web server and Telegram activity.
- Largest free block and heap fragmentation where available.
- Queue file sizes after an offline stress test.

Recommended runtime acceptance targets:

- No allocation failure or watchdog reset.
- Stable free heap during an eight-hour test.
- At least 12 KB free heap during normal operation.
- At least 8 KB free heap during TLS transmission peaks.
- Firmware partition has sufficient room for OTA when OTA is enabled.

These are engineering acceptance targets, not guarantees; actual limits must be confirmed on the selected ESP8266 module and flash layout.

## Phase 5 — Zero-error release build

A release candidate is eligible for merge or tagging only when:

1. Local Arduino IDE compilation succeeds with ESP8266 core 3.1.2.
2. GitHub Actions compilation succeeds.
3. The generated binary artifact is retained with the workflow run.
4. No unresolved compiler or linker errors remain.
5. Phase 3 hardware tests pass.
6. Phase 4 memory results are recorded and accepted.
7. Oracle backend health, telemetry ingestion and dashboard updates are verified.
8. The tested commit SHA is recorded before flashing production controllers.

## Release evidence template

- Commit SHA:
- ESP8266 board/module:
- Flash layout:
- Arduino IDE version:
- ESP8266 core: 3.1.2
- Local compile: PASS / FAIL
- GitHub Actions run:
- Binary size:
- Static RAM:
- IRAM:
- Startup free heap:
- Minimum free heap:
- Eight-hour test: PASS / FAIL
- Offline queue recovery: PASS / FAIL
- AFMS telemetry: PASS / FAIL
- Google Sheets: PASS / FAIL
- HMI/Modbus: PASS / FAIL
- Approved by:
- Date:
