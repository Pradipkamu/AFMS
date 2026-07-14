# AI Factory Monitoring System (AFMS)

AFMS is an Arduino IDE firmware project for the Wemos D1 Mini (ESP8266). It is designed as the machine-side controller for production monitoring, HMI integration, OEE calculation, loss tracking, Google Sheets reporting, Telegram notifications, and a future factory dashboard.

## Commit 0001 scope

- Arduino entry point
- Serial logger
- LittleFS initialization
- JSON-like machine configuration loader
- Non-blocking Wi-Fi connection manager
- Fixed-size event queue
- Machine engine lifecycle foundation

## Arduino IDE setup

1. Install Arduino IDE 2.x.
2. Install **ESP8266 by ESP8266 Community** from Boards Manager.
3. Select **LOLIN(WEMOS) D1 R2 & mini**.
4. Use a flash layout that reserves at least 1 MB for LittleFS.
5. Open `AFMS.ino` and verify the sketch.
6. Upload the contents of `data/` to LittleFS using a compatible Arduino IDE filesystem uploader.

## Configuration

Edit `data/machine.json` before uploading the LittleFS image:

```json
{
  "machine_id": "MCH001",
  "machine_name": "PRESS-01",
  "wifi_ssid": "YOUR_WIFI_SSID",
  "wifi_password": "YOUR_WIFI_PASSWORD"
}
```

Do not commit real Wi-Fi passwords to a public repository.

## Hardware target

- Wemos D1 Mini / ESP8266
- Delta DOP-107CV HMI
- MAX485 RS-485 converter
- NPN production proximity sensor
- NPN rejection proximity sensor
- Idle/loss alarm output

## Version

`3.0.0-dev01`
