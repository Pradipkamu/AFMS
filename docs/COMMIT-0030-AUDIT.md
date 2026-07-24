# AFMS Commit 0030A-G Audit

Audit date: 2026-07-24

## Executive result

The architecture, server configuration model, device registry, API-key authentication, configurable scheduling, remote configuration download, and persistent queue storage are present. Commit 0030 is **not yet end-to-end complete** because the firmware sender, trigger wiring, queue replay, and secure telemetry integration are still missing.

## Completed and usable foundations

- Stable rollback branch exists: `backup/stable-before-commit-0030`.
- Communication Manager reads machine-wise AFMS Web and Google Sheets schedules.
- Server stores versioned machine configurations and configuration history.
- ESP checks the server for configuration updates and retains local operation when unavailable.
- Device API keys are generated once and stored as SHA-256 hashes server-side.
- Secure device configuration endpoint binds each API key to one machine.
- Secure telemetry endpoint validates fields and rejects machine impersonation.
- `eventId` deduplication exists server-side.
- Separate bounded LittleFS queues exist for AFMS Web and Google Sheets.

## Critical missing phases

### 1. AFMS firmware sender is missing

No firmware component currently builds telemetry JSON and POSTs it to `/api/v1/devices/telemetry` with `X-AFMS-Device-Key`.

Required:

- `AfmsWebClient` or equivalent module.
- Stable `eventId` generation.
- Machine status, production, reject, cycle-time, loss and firmware fields.
- Success/failure reporting to `CommunicationManager`.
- HTTP timeout and retry/backoff behavior.

### 2. Scheduling output is not consumed

`CommunicationManager::webDue()` and `googleDue()` expose due states, but no transport layer consumes those states and marks completion.

Required:

- Sender calls when each destination becomes due.
- `markWebComplete()` and `markGoogleComplete()` after result.
- Prevention of overlapping requests.

### 3. Machine events are not wired to scheduler triggers

The trigger API exists, but machine status, loss, shift and production milestone events are not yet connected to `CommunicationManager::notify()`.

Required:

- Status-change trigger from Machine Engine.
- Loss-change trigger from loss acknowledgement/state handling.
- Shift-change trigger from Shift Manager.
- Production milestone trigger from cumulative production count.

### 4. Persistent queues are not connected to failed transmissions

The queues survive reboot, but no sender currently enqueues a failed payload or replays the queue after reconnection.

Required:

- Enqueue exact payload and event ID after a retryable failure.
- Replay oldest record first.
- Remove only after HTTP success or duplicate acknowledgement.
- Independent AFMS and Google replay loops.
- Exponential backoff and per-loop processing limits.

### 5. Secure telemetry bypasses existing business logic

The secure `/api/v1/devices/telemetry` endpoint inserts telemetry but does not currently:

- broadcast the live SSE `telemetry` event;
- open or close downtime records;
- execute the same processing path as `/api/v1/telemetry`.

Required:

- Extract one shared telemetry ingestion service.
- Route both legacy and authenticated endpoints through it during migration.
- Broadcast live dashboard updates and apply downtime lifecycle consistently.

### 6. Remote configuration uses plain HTTP

The current ESP implementation uses `WiFiClient`, so the device key and configuration are not encrypted over the Internet.

Required before production:

- Domain and valid TLS certificate.
- `WiFiClientSecure` certificate validation or pinned trust strategy.
- HTTPS-only AFMS server URL.

### 7. Device key is stored as plain text on LittleFS

The API key must exist on the ESP, but access controls and provisioning procedures are not documented.

Required:

- One-time provisioning workflow.
- Redaction from diagnostics and configuration webpages.
- Never include the key in normal logs or exports.
- Key rotation test and recovery process.

### 8. Configuration enable flag is not enforced by RemoteConfigManager

`communication.configSync.enabled` exists in the configuration model, but the firmware remote-config loop does not currently stop checking when it is false.

Required:

- Read and enforce `configSync.enabled`.
- Preserve a manual force-sync option for maintenance.

### 9. Configuration rollback retention is too short

The remote config backup is deleted immediately after activation. If a new configuration loads successfully but causes a later operational issue, no previous remote version remains locally.

Recommended:

- Keep one last-known-good file until the new configuration passes a stability period.
- Record active and previous config versions.
- Add explicit local rollback.

### 10. Compile and integration automation is missing

The GitHub commits were structurally reviewed but were not proven by an automated ESP8266 compile or backend test workflow.

Required:

- Arduino CLI or PlatformIO compile in GitHub Actions.
- Backend startup/API integration tests.
- Database migration test.
- Queue reboot/replay test.
- Invalid configuration and invalid key tests.

## Commit 0030H status

The Device Management Dashboard has not yet been implemented. It should be built only after the critical end-to-end sender and ingestion gaps above are corrected.

The page should support:

- device registration;
- one-time API-key display;
- key rotation and disabling;
- machine configuration editing and validation;
- configuration version/history;
- last seen, last configuration sync and firmware version;
- queue/communication health where reported by the ESP.

## Recommended corrective sequence

1. 0030G1: Shared backend telemetry ingestion service.
2. 0030G2: ESP authenticated AFMS Web sender.
3. 0030G3: Trigger wiring and production milestone tracking.
4. 0030G4: Queue enqueue, replay and backoff integration.
5. 0030G5: TLS and secret-handling hardening.
6. 0030G6: Automated compile and integration tests.
7. 0030H: Device Management Dashboard.

## Production decision

Do not provision production API keys or disable the existing Google Sheets path yet. Continue testing the current working firmware while the corrective sequence is completed. The rollback branch remains the recovery source.
