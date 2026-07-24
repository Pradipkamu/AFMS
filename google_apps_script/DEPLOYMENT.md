# AFMS Google Apps Script V3.1.1 deployment

The firmware and Apps Script must be deployed as a matched V3.1.1 pair.

## Existing spreadsheet

- File: `AFMS Monitering`
- Spreadsheet ID: `1eAbQ1RlFRNcyOolsraL6AzNmsDtcI_NijozKbs6Xa6c`
- Existing tabs retained: `Events`, `Hourly_Summary`, `Shift_Summary`, `OEE`

## Install

1. Open the `AFMS Monitering` spreadsheet.
2. Select **Extensions → Apps Script**.
3. Replace the current `Code.gs` contents with `google_apps_script/Code.gs` from this branch.
4. In Apps Script, open **Project Settings → Script Properties**.
5. Add property:
   - Name: `AFMS_API_TOKEN`
   - Value: the same token used by `api_token` in `/machine.json`
6. Save the project.
7. Select **Deploy → Manage deployments**.
8. Edit the existing web-app deployment or create a new one.
9. Execute as: **Me**.
10. Access: choose the same access level used by the current AFMS deployment.
11. Deploy and copy the web-app `/exec` URL.
12. Put that URL into `google_web_app_url` in `/machine.json`.
13. Reboot the ESP8266 after updating the file.

## V3.1.1 behavior

- Requires `report_id` in every POST.
- Uses a hidden `Report_Index` sheet to suppress duplicate reports.
- Returns HTTP 200 JSON with `duplicate:true` when a report was already stored.
- Routes events and losses to `Events`.
- Routes hourly records to `Hourly_Summary`.
- Routes shift and recovered-shift records to `Shift_Summary`.
- Routes statistics and OEE records to `OEE`.
- Writes `loss_name` into the `Loss Name` column.
- Preserves existing sheet rows and adds missing headers.

## Test

After deployment, open the web-app URL in a browser. Expected response:

```json
{"ok":true,"service":"AFMS","version":"3.1.1"}
```

Then send one loss from AFMS and confirm:

- `Report ID` is populated.
- `Loss Code` is populated.
- `Loss Name` is populated.
- `Loss Duration Seconds` is populated.
- Retrying the same outbox report does not append another row.
