# AFMS v3.1 Reporting Architecture

## Approved scope

- Remove Machine Ready from Google and Telegram reporting.
- Store each pending report as one file in `/outbox`.
- Preserve pending Google and Telegram deliveries across restart and Wi-Fi loss.
- Continue generating hourly summaries, loss events, shift summaries, and shift CSV files.
- At month rollover, queue the completed month's report files for Telegram.
- Delete completed-month data only after every required Telegram document is confirmed sent.
- Do not create ZIP files on ESP8266. Send monthly CSV files individually for reliability.
- Add LittleFS capacity monitoring and priority-based cleanup.

## Outbox file format

Path:

```text
/outbox/R_<created_epoch>_<sequence>.json
```

Required fields:

```json
{
  "schema": 1,
  "report_id": "M01-S2-1784563200-0001",
  "type": "shift_summary",
  "created_epoch": 1784563200,
  "priority": 110,
  "google_required": true,
  "google_sent": false,
  "telegram_required": false,
  "telegram_sent": false,
  "document_path": "",
  "payload": {}
}
```

Each record is written to a temporary file and atomically renamed to its final path. A report may be removed only when all required destinations are acknowledged.

## Priorities

| Report type | Priority |
|---|---:|
| Recovered shift summary | 120 |
| Normal shift summary | 110 |
| Monthly Telegram document | 100 |
| Loss event | 80 |
| Hourly summary | 40 |

Machine Ready is not an outbox report.

## Delivery rules

### Google

- Consume the oldest eligible outbox report requiring Google.
- On success, atomically set `google_sent=true`.
- Use exponential retry from 30 seconds up to 15 minutes.
- Do not use a reporting row as a connectivity probe.

### Telegram

- Wi-Fi state changes may reset bot verification only.
- Wi-Fi state changes must not clear pending loss or document deliveries.
- Consume the oldest eligible outbox report requiring Telegram.
- On success, atomically set `telegram_sent=true`.
- Retry failed sends without deleting the report.

## Month rollover

When synchronized time enters a new month:

1. Identify all report files belonging to the completed month.
2. Finalize the previous month's shift CSV.
3. Create persistent Telegram document outbox records for each monthly CSV file.
4. Send files individually.
5. Keep the previous month's files until every document outbox record is acknowledged.
6. Delete acknowledged previous-month report data and temporary files.
7. Never delete the current month's CSV.

A restart at any step must resume the same month-close transaction without duplicating or losing required files.

## LittleFS cleanup

Thresholds:

- Warning: 80% used.
- Cleanup start: 85% used.
- Cleanup stop: 70% used.
- Critical: unable to reserve space for a high-priority report.

Deletion order:

1. Old completed hourly reports.
2. Old completed loss events.
3. Old completed normal shift reports already delivered everywhere.
4. Old monthly files whose Telegram delivery is confirmed.

Never automatically delete:

- Any report with an unsent required destination.
- Any recovered shift report that is not fully delivered.
- Runtime state, configuration, or credentials.
- Current-month CSV files.
- Active transaction temporary or backup files.

## Migration

1. Introduce `ReportOutboxManager` alongside the existing `/offline.queue`.
2. Route new shift reports through the outbox first.
3. Route loss and hourly reports through the outbox.
4. Move Telegram document delivery to the outbox.
5. Import valid legacy `/offline.queue` and `/hourly.pending` records.
6. Remove legacy queue code only after field testing confirms delivery and reboot recovery.

## Acceptance tests

- Machine Ready creates no Google row and no Telegram message.
- Power loss immediately after shift closure does not lose the shift report.
- Wi-Fi loss does not clear a pending Telegram loss or CSV.
- Reboot resumes Google and Telegram delivery.
- Month rollover queues previous-month files exactly once.
- Previous-month files remain until Telegram confirms every document.
- LittleFS cleanup never removes an unsent report.
- Power failure during outbox update restores either the previous valid file or the new valid file.
