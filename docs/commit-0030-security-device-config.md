# Commit 0030 — Security, Device Configuration and Dual-Destination Communication

## Stable rollback point

The complete repository state before Commit 0030 is preserved at:

- Branch: `backup/stable-before-commit-0030`
- Commit: `0ed6a8aafc9f55ca1417498a3e5eb679ea94c20b`

Do not modify or force-update that backup branch.

## Implementation order

1. Preserve the current working firmware and web stack.
2. Introduce a non-blocking firmware communication manager.
3. Add machine-specific configuration storage and versioning on the AFMS server.
4. Add authenticated configuration download to ESP8266.
5. Add independent AFMS Web and Google Sheets destination settings.
6. Add event-driven, interval, production-milestone and hybrid transmission modes.
7. Add last-known-good LittleFS configuration with compiled-default fallback.
8. Add independent offline queues and retry policies.
9. Add device registration, API-key rotation and machine configuration screens.
10. Enforce human role permissions separately from device authentication.

## Configuration precedence

1. Valid configuration downloaded from AFMS.
2. Last-known-good `/machine.json` stored in LittleFS.
3. Compiled safe defaults.

A failed download or invalid JSON must never replace the last-known-good configuration.

## Destination behavior

AFMS Web and Google Sheets are independently configurable. Either, both or neither may be enabled.

AFMS Web supports:

- `INTERVAL`
- `MILESTONE`
- `HYBRID`
- immediate status/loss/shift events
- heartbeat transmission

Google Sheets retains its current summary-oriented behavior and has an independent upload interval.

## Proposed machine configuration shape

```json
{
  "machine_id": "MCH001",
  "config_version": 1,
  "config_sync": {
    "enabled": true,
    "check_interval_seconds": 300
  },
  "communication": {
    "afms_web": {
      "enabled": true,
      "server_url": "https://example.invalid/api/v1/device/telemetry",
      "mode": "HYBRID",
      "interval_seconds": 30,
      "heartbeat_seconds": 60,
      "production_milestone": 10,
      "send_on_status_change": true,
      "send_on_loss_change": true,
      "send_on_shift_change": true
    },
    "google_sheets": {
      "enabled": true,
      "upload_interval_seconds": 3600,
      "send_breakdown_immediately": true
    }
  }
}
```

## Safety boundaries

Remote updates may change communication and reporting settings. They must not remotely change GPIO mappings, safety outputs, calibration, HMI register maps or machine-control logic in Commit 0030.

## Validation limits

- AFMS interval: 5–3600 seconds
- Heartbeat: 15–3600 seconds
- Configuration sync: 60–86400 seconds
- Production milestone: 1–10000 parts
- Configuration version: monotonically increasing integer
- Machine ID in downloaded configuration must match the authenticated device

## Delivery policy

Commit 0030 remains on its feature branch until firmware compilation, LittleFS migration, Google Sheets regression, AFMS telemetry, offline fallback and server authentication tests all pass.
