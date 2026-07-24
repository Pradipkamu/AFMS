# Commit 0030S-P1 – Real-Time Performance Audit

## Scope

Audit the reported lag in production interrupt counting and machine-status changes after the expanded AFMS Web, Google Sheets, Telegram, remote-config and diagnostics integrations.

## Confirmed architecture

The firmware processes machine inputs first in `loop()`, before Wi-Fi and cloud work. However, all HTTP/TLS requests still run synchronously in the same ESP8266 main loop. A slow request therefore prevents `MachineEngine::update()` from consuming queued input events until the request completes.

## Critical findings

### P1 – Blocking network calls can stall the main loop for up to 10–15 seconds

`HttpClientManager` uses a 10,000 ms timeout for Google requests. AFMS Web posts are synchronous. Telegram uses 10,000–15,000 ms blocking request windows. During any of these operations, the next `MachineEngine::update()` is delayed.

Impact:

- Machine status changes appear late.
- Production pulses remain in the interrupt queue until the HTTP call returns.
- HMI status updates are delayed.
- Several cloud clients can block sequentially in one loop pass.

### P2 – Production ISR queue capacity is only seven usable events

`ProductionManager` declares an eight-slot ring buffer, with one slot reserved to distinguish full from empty. Only seven pulses can wait while the main loop is blocked. Additional valid pulses increment `gDropped` and are permanently lost.

At a 1-second cycle, a 10-second HTTP stall can overflow the queue. At faster cycle times, overflow occurs sooner.

### P3 – Cloud modules can perform more than one blocking action in one loop pass

The loop calls `AfmsWebClient::update()` and then `CloudManager::update()`. `CloudManager::update()` also runs Telegram processing and Google queue replay. A due AFMS Web request followed by Telegram or Google activity can create cumulative blocking time.

### P4 – Offline queue replay is attempted before new AFMS Web telemetry

`AfmsWebClient::update()` calls queue replay before handling a newly due telemetry record. A slow or unavailable server can therefore delay current machine-state delivery and then immediately attempt another network operation.

### P5 – Configuration reload and file reads add non-critical work to the real-time loop

`CommunicationManager` reparses `server.json` every 60 seconds. AFMS Web credentials are reread from `device.json` and `server.json` for every send. These are smaller delays than HTTP, but they add filesystem work to the same loop that consumes interrupt events.

### P6 – Large JSON serialization is secondary, not the primary cause

The expanded 2 KB telemetry document increases heap use and serialization time, but this is normally milliseconds or less. The dominant source of visible lag is synchronous network I/O.

### P7 – Current diagnostics expose dropped pulses but do not report them periodically

`ProductionManager::droppedPulseCount()` exists, but the normal serial health report does not surface it. The firmware therefore cannot currently distinguish delayed processing from actual pulse loss without code-level inspection.

## Risk ranking

| Finding | Severity | Probability |
|---|---:|---:|
| Blocking HTTP/TLS in main loop | Critical | High |
| Seven-event production queue | Critical | High during network faults |
| Sequential cloud operations | High | High |
| Replay before current telemetry | High | Medium |
| Repeated JSON/file parsing | Medium | Medium |
| Expanded JSON serialization | Low | Medium |

## Required optimization order

1. Increase production and reject interrupt queue capacity and expose queue-overflow diagnostics.
2. Enforce a strict communication time budget: at most one network transaction per loop scheduling window.
3. Prioritize fresh machine telemetry over historical queue replay.
4. Reduce HTTP connect/read timeouts and apply retry backoff instead of waiting 10–15 seconds.
5. Cache device credentials and server communication policy in RAM.
6. Add loop-stall, maximum-loop-duration and dropped-pulse reporting.
7. Split cloud processing into small state-machine steps where possible.

## Acceptance criteria

- Zero dropped production pulses during a 10-minute test with Oracle disconnected.
- Machine status reflected on HMI within 200 ms when no Modbus transport delay exists.
- Maximum normal loop duration below 20 ms.
- No single cloud operation blocks machine processing for more than 250 ms; longer network work must be deferred or split.
- Only one outbound network transaction is started in a scheduling pass.
- Offline replay never delays a fresh loss/status event.

## Immediate test procedure

1. Enable serial diagnostics.
2. Run the machine with AFMS Web, Google Sheets and Telegram disabled; record response time.
3. Enable only AFMS Web and disconnect Oracle; record response time and dropped pulses.
4. Enable only Google Sheets with an invalid/unreachable endpoint; repeat.
5. Enable Telegram with Internet disconnected; repeat.
6. Enable all destinations and force all schedules due together.
7. Compare production input pulses with HMI/firmware totals.

## Audit conclusion

The interrupt itself is lightweight and correctly stores timestamps in IRAM. The observed lag is primarily a main-loop starvation problem caused by synchronous HTTP/TLS operations, amplified by the small production pulse queue. The next commit must focus on real-time scheduling and queue protection before further feature work or production rollout.
