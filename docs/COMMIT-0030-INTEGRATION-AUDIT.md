# Commit 0030 Full Integration Audit

Status: In progress

Scope: Firmware modules introduced or modified by Commit 0030A-G, plus the AFMS backend and dashboard integration points.

## Confirmed compile blocker

`CloudManager.cpp` still uses the legacy queue API (`push`, `peek(String&)`, `pop`) while `OfflineQueue` exposes the new destination-aware API. This prevents ESP8266 compilation.

## Required stabilization checks

- Restore backward-compatible Google queue wrappers or migrate every caller.
- Verify only one module initializes the offline queue.
- Verify AFMS Web and Google queues remain independent.
- Verify queue event IDs are unique enough across reboots.
- Wire communication due flags to actual sender execution.
- Wire machine, loss, shift and production events to CommunicationManager triggers.
- Add AFMS authenticated sender and queue replay.
- Route authenticated telemetry through the same downtime and live-event processing path as legacy telemetry.
- Enforce configSync.enabled.
- Replace plain HTTP device-key transport with HTTPS before production.
- Add automated ESP8266 compile and backend integration tests.

## Current conclusion

Commit 0030G provides queue storage primitives, but the complete end-to-end sender and replay path is not yet implemented. The immediate compile mismatch must be fixed before further testing.