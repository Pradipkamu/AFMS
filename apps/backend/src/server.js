import 'dotenv/config';
import cors from 'cors';
import express from 'express';
import helmet from 'helmet';
import pg from 'pg';

const { Pool } = pg;
const app = express();
const port = Number(process.env.PORT || 3000);
const pool = new Pool({ connectionString: process.env.DATABASE_URL });

app.use(helmet());
app.use(cors());
app.use(express.json({ limit: '64kb' }));

app.get('/api/health', async (_req, res) => {
  try {
    await pool.query('SELECT 1');
    res.json({ status: 'ok', service: 'afms-backend', database: 'connected' });
  } catch (error) {
    res.status(503).json({ status: 'degraded', service: 'afms-backend', database: 'unavailable' });
  }
});

app.post('/api/v1/telemetry', async (req, res) => {
  const { machineId, status, productionCount = 0, rejectCount = 0, cycleTimeMs = 0, lossCode = null, recordedAt } = req.body;
  if (!machineId || !status) {
    return res.status(400).json({ error: 'machineId and status are required' });
  }

  const result = await pool.query(
    `INSERT INTO machine_telemetry
      (machine_id, status, production_count, reject_count, cycle_time_ms, loss_code, recorded_at)
     VALUES ($1, $2, $3, $4, $5, $6, COALESCE($7::timestamptz, NOW()))
     RETURNING id, recorded_at`,
    [machineId, status, productionCount, rejectCount, cycleTimeMs, lossCode, recordedAt || null]
  );
  res.status(201).json(result.rows[0]);
});

app.get('/api/v1/machines/latest', async (_req, res) => {
  const result = await pool.query(`
    SELECT DISTINCT ON (machine_id)
      machine_id AS "machineId", status, production_count AS "productionCount",
      reject_count AS "rejectCount", cycle_time_ms AS "cycleTimeMs",
      loss_code AS "lossCode", recorded_at AS "recordedAt"
    FROM machine_telemetry
    ORDER BY machine_id, recorded_at DESC
  `);
  res.json(result.rows);
});

app.use((error, _req, res, _next) => {
  console.error(error);
  res.status(500).json({ error: 'Internal server error' });
});

app.listen(port, '0.0.0.0', () => {
  console.log(`AFMS backend listening on port ${port}`);
});
