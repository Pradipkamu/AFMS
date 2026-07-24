const ACTIVE_LOSS_STATUSES = new Set(['STOPPED', 'BREAKDOWN', 'IDLE']);

function normalizeDowntime(row) {
  if (!row) return row;
  return {
    id: row.id,
    machineId: row.machine_id,
    lossCode: row.loss_code,
    category: row.category,
    status: row.status,
    startedAt: row.started_at,
    acknowledgedAt: row.acknowledged_at,
    acknowledgedBy: row.acknowledged_by,
    note: row.note,
    closedAt: row.closed_at,
    createdAt: row.created_at
  };
}

export function validateTelemetry(body, expectedMachineId = null) {
  const statuses = ['RUNNING', 'IDLE', 'SETUP', 'STOPPED', 'BREAKDOWN', 'OFFLINE'];
  if (!body?.machineId || !body?.status) return 'machineId and status are required';
  if (expectedMachineId && body.machineId !== expectedMachineId) return 'Device key is not assigned to this machine';
  if (!statuses.includes(String(body.status).toUpperCase())) return 'Invalid machine status';
  for (const name of ['productionCount', 'rejectCount', 'cycleTimeMs']) {
    if (body[name] != null && (!Number.isInteger(body[name]) || body[name] < 0)) return `${name} must be a non-negative integer`;
  }
  if ((body.rejectCount ?? 0) > (body.productionCount ?? 0)) return 'rejectCount cannot exceed productionCount';
  if (body.recordedAt && Number.isNaN(Date.parse(body.recordedAt))) return 'recordedAt must be a valid timestamp';
  return null;
}

export async function ingestTelemetry(pool, body, broadcast) {
  const {
    machineId,
    status,
    productionCount = 0,
    rejectCount = 0,
    cycleTimeMs = 0,
    lossCode = null,
    recordedAt = null
  } = body;

  const normalizedStatus = String(status).toUpperCase();
  const result = await pool.query(
    `INSERT INTO machine_telemetry(machine_id,status,production_count,reject_count,cycle_time_ms,loss_code,recorded_at)
     VALUES($1,$2,$3,$4,$5,$6,COALESCE($7::timestamptz,NOW()))
     RETURNING id,machine_id AS "machineId",status,production_count AS "productionCount",reject_count AS "rejectCount",cycle_time_ms AS "cycleTimeMs",loss_code AS "lossCode",recorded_at AS "recordedAt"`,
    [machineId, normalizedStatus, productionCount, rejectCount, cycleTimeMs, lossCode, recordedAt]
  );
  const telemetry = result.rows[0];

  if (lossCode || ACTIVE_LOSS_STATUSES.has(normalizedStatus)) {
    const code = lossCode || normalizedStatus;
    const open = await pool.query(
      `SELECT id FROM downtime_events
       WHERE machine_id=$1 AND status IN ('OPEN','ACKNOWLEDGED')
       ORDER BY started_at DESC LIMIT 1`,
      [machineId]
    );
    if (!open.rowCount) {
      const created = await pool.query(
        `INSERT INTO downtime_events(machine_id,loss_code,category,started_at)
         VALUES($1,$2,$3,COALESCE($4::timestamptz,NOW())) RETURNING *`,
        [machineId, code, normalizedStatus, recordedAt]
      );
      broadcast?.('downtime', normalizeDowntime(created.rows[0]));
    }
  } else if (normalizedStatus === 'RUNNING') {
    const closed = await pool.query(
      `UPDATE downtime_events
       SET status='CLOSED',closed_at=COALESCE($2::timestamptz,NOW())
       WHERE machine_id=$1 AND status IN ('OPEN','ACKNOWLEDGED') RETURNING *`,
      [machineId, recordedAt]
    );
    for (const row of closed.rows) broadcast?.('downtime', normalizeDowntime(row));
  }

  broadcast?.('telemetry', telemetry);
  return telemetry;
}
