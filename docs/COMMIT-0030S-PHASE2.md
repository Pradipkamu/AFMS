# Commit 0030S Phase 2 — Full Module and Linker Verification

Status: In progress

## Objective
Compile the complete ESP8266 firmware with ESP8266 Arduino Core 3.1.2 and verify every translation unit, public header, namespace, symbol, and linker dependency.

## Verification scope
- AFMS.ino startup and loop integration
- Config and Version APIs
- MachineEngine and ShiftManager
- CommunicationManager
- AfmsWebClient
- CloudManager and Google Sheets upload path
- RemoteConfigManager
- HttpClientManager and WiFiManager
- OfflineQueue destination-aware APIs
- TelegramClient
- WebManager and OTA manager
- LittleFS managers and runtime persistence
- HMI manager and event bus

## Exit criteria
- No missing headers
- No obsolete API calls
- No duplicate definitions
- No unresolved external symbols
- No namespace/type mismatches
- No linker errors
- GitHub Actions compile succeeds
- Local Arduino IDE compile succeeds with ESP8266 core 3.1.2

## Current finding
The first Phase 1 GitHub Actions build reached the Compile AFMS step and failed. Environment setup, ESP8266 core installation, and library installation completed successfully. Phase 2 will use the exact compiler/linker output to remove the next blocking error before continuing.
