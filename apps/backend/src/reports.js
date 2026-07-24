import express from 'express';

function csvCell(value) {
  const text = value == null ? '' : String(value);
  return /[",\n]/.test(text) ? `"${text.replaceAll('"', '""')}"` : text;
}

export function createReportsRouter(pool) {
  const router = express.Router();

  router.get('/summary', async (req, res, next) => {
    try {
      const from = req.query.from || new Date(Date.now() - 24 * 3600_000).toISOString();
      const to = req.query.to || new Date().toISOString();
      const machineId = String(req.query.machineId || '');

      const production = await pool.query(`
        WITH ranked AS (
          SELECT machine_id, production_count, reject_count, recorded_at,
            ROW_NUMBER() OVER (PARTITION BY machine_id ORDER BY recorded_at, id) AS first_rank,
            ROW_NUMBER() OVER (PARTITION BY machine_id ORDER BY recorded_at DESC, id DESC) AS last_rank
          FROM machine_telemetry
          WHERE recorded_at BETWEEN $1::timestamptz AND $2::timestamptz
            AND ($3='' OR machine_id=$3)
        )
        SELECT machine_id AS "machineId",
          GREATEST(0, MAX(production_count) FILTER (WHERE last_rank=1) - MAX(production_count) FILTER (WHERE first_rank=1)) AS "totalParts",
          GREATEST(0, MAX(reject_count) FILTER (WHERE last_rank=1) - MAX(reject_count) FILTER (WHERE first_rank=1)) AS "rejectParts"
        FROM ranked GROUP BY machine_id ORDER BY machine_id`, [from, to, machineId]);

      const downtime = await pool.query(`
        SELECT machine_id AS "machineId", loss_code AS "lossCode", category,
          COUNT(*)::int AS events,
          SUM(EXTRACT(EPOCH FROM (LEAST(COALESCE(closed_at,$2::timestamptz),$2::timestamptz) - GREATEST(started_at,$1::timestamptz))))::bigint AS "durationSeconds"
        FROM downtime_events
        WHERE started_at < $2::timestamptz AND COALESCE(closed_at,$2::timestamptz) > $1::timestamptz
          AND ($3='' OR machine_id=$3)
        GROUP BY machine_id, loss_code, category
        ORDER BY "durationSeconds" DESC`, [from, to, machineId]);

      const rows = production.rows.map((row) => {
        const totalParts = Number(row.totalParts || 0);
        const rejectParts = Number(row.rejectParts || 0);
        return { ...row, totalParts, rejectParts, goodParts: Math.max(0, totalParts - rejectParts), quality: totalParts ? ((totalParts - rejectParts) / totalParts) * 100 : 100 };
      });

      res.json({ generatedAt: new Date().toISOString(), from, to, machineId: machineId || null, production: rows, downtime: downtime.rows });
    } catch (error) { next(error); }
  });

  router.get('/telemetry.csv', async (req, res, next) => {
    try {
      const from = req.query.from || new Date(Date.now() - 24 * 3600_000).toISOString();
      const to = req.query.to || new Date().toISOString();
      const machineId = String(req.query.machineId || '');
      const result = await pool.query(`SELECT machine_id AS "machineId", status, production_count AS "productionCount", reject_count AS "rejectCount", cycle_time_ms AS "cycleTimeMs", loss_code AS "lossCode", recorded_at AS "recordedAt" FROM machine_telemetry WHERE recorded_at BETWEEN $1::timestamptz AND $2::timestamptz AND ($3='' OR machine_id=$3) ORDER BY recorded_at`, [from, to, machineId]);
      const header = ['machineId','status','productionCount','rejectCount','cycleTimeMs','lossCode','recordedAt'];
      const csv = [header.join(','), ...result.rows.map((row) => header.map((key) => csvCell(row[key])).join(','))].join('\n');
      res.set({ 'Content-Type': 'text/csv; charset=utf-8', 'Content-Disposition': 'attachment; filename="afms-telemetry.csv"' });
      res.send(csv);
    } catch (error) { next(error); }
  });

  return router;
}
