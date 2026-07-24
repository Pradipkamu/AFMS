# AFMS — Automated Factory Monitoring System

AFMS is a full-stack factory monitoring platform for ESP8266 machine controllers, Delta HMI integration, production and reject counting, loss acknowledgement, OEE reporting, and a browser-based dashboard hosted on Oracle Cloud.

## Repository layout

```text
AFMS/
├── AFMS.ino, src/, data/       Existing ESP8266 firmware (migration-safe)
├── apps/
│   ├── backend/                Node.js REST API
│   └── frontend/               React/Vite dashboard
├── database/                   PostgreSQL schema and migrations
├── deploy/nginx/               Reverse-proxy configuration
├── docs/                       Architecture and operating documentation
├── Tools/                      Existing firmware utilities
├── compose.yaml                Oracle VM production stack
└── .env.example                Deployment settings template
```

The existing Arduino project remains in its current paths during the first migration phase so firmware builds are not broken. It will be moved under `firmware/esp8266/` only after the Arduino include paths, LittleFS workflow, and release process have been verified.

## Run the Oracle Cloud stack

Requirements: Docker Engine and Docker Compose plugin.

```bash
cp .env.example .env
nano .env                         # set a strong POSTGRES_PASSWORD
docker compose up -d --build
docker compose ps
curl http://localhost/api/health
```

The public entry point is Nginx on port 80. PostgreSQL and the Node.js API are available only inside the Docker network.

## ESP8266 telemetry endpoint

Send JSON to:

```text
POST /api/v1/telemetry
```

Example payload:

```json
{
  "machineId": "MCH001",
  "status": "RUNNING",
  "productionCount": 125,
  "rejectCount": 2,
  "cycleTimeMs": 12400,
  "lossCode": null
}
```

## Development

```bash
npm install
npm run dev:backend
npm run dev:frontend
```

## Migration roadmap

1. Establish backend, dashboard, PostgreSQL, Nginx, and Docker deployment.
2. Add authenticated ESP8266 ingestion and machine registration.
3. Add WebSocket live updates, shifts, OEE, downtime, and reports.
4. Move the verified firmware project to `firmware/esp8266/` without breaking Arduino builds.
5. Add automated testing and Oracle Cloud deployment workflows.

## Hardware target

- Wemos D1 Mini / ESP8266
- Delta DOP-107CV HMI
- MAX485 RS-485 converter
- Production and rejection proximity sensors
- Idle/loss alarm output
