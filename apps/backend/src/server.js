import 'dotenv/config';
import cors from 'cors';
import express from 'express';
import helmet from 'helmet';
import pg from 'pg';

const { Pool } = pg;
const app = express();
const port = Number(process.env.PORT || 3000);
const pool = new Pool({ connectionString: process.env.DATABASE_URL });
const eventClients = new Set();

const asyncHandler = (handler) => (req, res, next) => {
  Promise.resolve(handler(req, res, next)).catch(next);
};

function sendEvent(res, event, payload) {
  res.write(`event: ${event}\n`);
  res.write(`data: ${JSON.stringify(payload)}\n\n`);
}

function broadcast(event, payload) {
  for (const client of eventClients) {
    sendEvent(client, event, payload);
  }
}

app.use(helmet({
  crossOriginResourcePolicy: false,
}));
app.use(cors());
app.use(express.json({ limit: '64kb' }));

app.get('/api/health', asyncHandler(async (_req, res) => {
  await pool.query('SELECT 1');
  res.json({
    status: 'ok',
    service: 'afms-backend',
    database: 'connected',
    liveClients: eventClients.size,
  });
}));

app.get('/api/v1/events', (req, res) => {
  res.set({
    'Content-Type': 'text/event-stream',
    'Cache-Control': 'no-cache, no-transform',
    Connection: 'keep-alive',
    'X-Accel-Buffering': 'no',
  });
  res.flushHeaders();

  eventClients.add(res);
  sendEvent(res, 'connected', { connectedAt: new Date().toISOString() });

  const heartbeat = setInterval(() => {
    sendEvent(res, 'heartbeat', { timestamp: new Date().toISOString() });
  }, 20000);

  req.on('close', () => {
    clearInterval(heartbeat);
    eventClients.delete(res);
    res.end();
  });
});

app.post('/api/v1/telemetry', asyncHandler(async (req, res) => {
  const {
    machineId,
    status,
    productionCount = 0,
    rejectCount = 0,
    cycleTimeMs = 0,
    lossCode = null,
    recordedAt,
  } = req.body;

  if (!machineId || !status) {
    return res.status(400).json({ error: 'machineId and status are required' });
  }

  const result = await pool.query(
    `INSERT INTO machine_telemetry
      (machine_id, status, production_count, reject_count, cycle_time_ms, loss_code, recorded_at)
     VALUES ($1, $2, $3, $4, $5, $6, COALESCE($7::timestamptz, NOW()))
     RETURNING id,
       machine_id AS "machineId",
       status,
       production_count AS "productionCount",
       reject_count AS "rejectCount",
       cycle_time_ms AS "cycleTimeMs",
       loss_code AS "lossCode",
       recorded_at AS "recordedAt"`,
    [machineId, status, productionCount, rejectCount, cycleTimeMs, lossCode, recordedAt || null],
  );

  const telemetry = result.rows[0];
  broadcast('telemetry', telemetry);
  return res.status(201).json(telemetry);
}));

app.get('/api/v1/machines/latest', asyncHandler(async (_req, res) => {
  const result = await pool.query(`
    SELECT DISTINCT ON (machine_id)
      machine_id AS "machineId", status, production_count AS "productionCount",
      reject_count AS "rejectCount", cycle_time_ms AS "cycleTimeMs",
      loss_code AS "lossCode", recorded_at AS "recordedAt"
    FROM machine_telemetry
    ORDER BY machine_id, recorded_at DESC, id DESC
  `);
  res.json(result.rows);
}));

app.use((error, _req, res, _next) => {
  console.error(error);
  if (res.headersSent) return;
  res.status(500).json({ error: 'Internal server error' });
});

const server = app.listen(port, '0.0.0.0', () => {
  console.log(`AFMS backend listening on port ${port}`);
});

async function shutdown(signal) {
  console.log(`${signal} received, shutting down AFMS backend`);
  server.close(async () => {
    for (const client of eventClients) client.end();
    await pool.end();
    process.exit(0);
  });
}

process.on('SIGTERM', () => shutdown('SIGTERM'));
process.on('SIGINT', () => shutdown('SIGINT'));
