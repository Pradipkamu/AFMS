# AFMS Split Configuration

AFMS now uses two LittleFS configuration files.

## device.json

Local commissioning and secret-bearing values:

- machine identity and display name
- Wi-Fi credentials
- Google Apps Script URL and API token
- Telegram token and chat ID
- Oracle AFMS base URL
- device API key and TLS fingerprint
- input debounce and runtime-state persistence settings

`Config::save()` and the local configuration page update only `device.json`.

## server.json

Machine operating policy managed by Oracle AFMS:

- configuration version and update timestamp
- loss alarm delay and alarm polarity
- shift schedule
- AFMS Web enable, frequency, mode, heartbeat and triggers
- Google Sheets enable and upload frequency
- remote configuration synchronization settings

Remote configuration downloads are validated and atomically replace only `server.json`.

## Fallback order

1. Valid `device.json` plus valid `server.json`
2. Missing half uses compiled defaults for that half
3. Legacy `/machine.json` is read only as a migration fallback when either new file is missing
4. Invalid remote configuration is rejected and the last known good `server.json` remains active

## Upload

Upload the complete `data/` directory to LittleFS. Both files must be present for production testing.

## Security

Do not commit real passwords, API keys, bot tokens, or certificate fingerprints. Replace template values locally before uploading LittleFS.
