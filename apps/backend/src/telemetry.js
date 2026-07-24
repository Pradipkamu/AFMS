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

const NON_NEGATIVE_INTEGER_FIELDS = [
  'productionCount','rejectCount','goodCount','cycleTimeMs','lossDurationSeconds',
  'shiftId','operatorId','partNumber','idleSeconds','runSeconds','downtimeSeconds',
  'availabilityPermille','performancePermille','qualityPermille','oeePermille','freeHeap'
];

export function validateTelemetry(body, expectedMachineId = null) {
  const statuses = ['RUNNING', 'IDLE', 'SETUP', 'STOPPED', 'BREAKDOWN', 'OFFLINE'];
  if (!body?.machineId || !body?.status) return 'machineId and status are required';
  if (expectedMachineId && body.machineId !== expectedMachineId) return 'Device key is not assigned to this machine';
  if (!statuses.includes(String(body.status).toUpperCase())) return 'Invalid machine status';
  for (const name of NON_NEGATIVE_INTEGER_FIELDS) {
    if (body[name] != null && (!Number.isInteger(body[name]) || body[name] < 0)) return `${name} must be a non-negative integer`;
  }
  if (body.wifiRssi != null && (!Number.isInteger(body.wifiRssi) || body.wifiRssi < -150 || body.wifiRssi > 20)) return 'wifiRssi must be an integer between -150 and 20';
  if ((body.rejectCount ?? 0) > (body.productionCount ?? 0)) return 'rejectCount cannot exceed productionCount';
  if (body.goodCount != null && body.goodCount + (body.rejectCount ?? 0) > (body.productionCount ?? 0)) return 'goodCount plus rejectCount cannot exceed productionCount';
  for (const name of ['availabilityPermille','performancePermille','qualityPermille','oeePermille']) {
    if (body[name] != null && body[name] > 1000) return `${name} must be between 0 and 1000`;
  }
  const timestamp = body.recordedAt ?? body.timestamp;
  if (timestamp && Number.isNaN(Date.parse(timestamp))) return 'timestamp must be a valid timestamp';
  return null;
}

export async function ingestTelemetry(pool, body, broadcast) {
  const {
    machineId,
    machineName = null,
    status,
    productionCount = 0,
    rejectCount = 0,
    goodCount = Math.max(0, productionCount - rejectCount),
    cycleTimeMs = 0,
    lossCode = null,
    lossName = null,
    lossDurationSeconds = 0,
    shiftId = null,
    operatorId = null,
    partNumber = null,
    partName = null,
    alarmActive = false,
    idleSeconds = 0,
    runSeconds = 0,
    downtimeSeconds = 0,
    availabilityPermille = null,
    performancePermille = null,
    qualityPermille = null,
    oeePermille = null,
    wifiRssi = null,
    freeHeap = null,
    firmwareVersion = null,
    recordedAt = null,
    timestamp = null
  } = body;

  const normalizedStatus = String(status).toUpperCase();
  const eventTime = recordedAt ?? timestamp;
  const result = await pool.query(
    `INSERT INTO machine_telemetry(
       machine_id,machine_name,status,production_count,reject_count,good_count,cycle_time_ms,
       loss_code,loss_name,loss_duration_seconds,shift_id,operator_id,part_number,part_name,
       alarm_active,idle_seconds,run_seconds,downtime_seconds,availability_permille,
       performance_permille,quality_permille,oee_permille,wifi_rssi,free_heap,
       firmware_version,recorded_at)
     VALUES($1,$2,$3,$4,$5,$6,$7,$8,$9,$10,$11,$12,$13,$14,$15,$16,$17,$18,$19,$20,$21,$22,$23,$24,$25,COALESCE($26::timestamptz,NOW()))
     RETURNING id,machine_id AS "machineId",machine_name AS "machineName",status,
       production_count AS "productionCount",reject_count AS "rejectCount",good_count AS "goodCount",
       cycle_time_ms AS "cycleTimeMs",loss_code AS "lossCode",loss_name AS "lossName",
       loss_duration_seconds AS "lossDurationSeconds",shift_id AS "shiftId",operator_id AS "operatorId",
       part_number AS "partNumber",part_name AS "partName",alarm_active AS "alarmActive",
       idle_seconds AS "idleSeconds",run_seconds AS "runSeconds",downtime_seconds AS "downtimeSeconds",
       availability_permille AS "availabilityPermille",performance_permille AS "performancePermille",
       quality_permille AS "qualityPermille",oee_permille AS "oeePermille",wifi_rssi AS "wifiRssi",
       free_heap AS "freeHeap",firmware_version AS "firmwareVersion",recorded_at AS "recordedAt"`,
    [machineId,machineName,normalizedStatus,productionCount,rejectCount,goodCount,cycleTimeMs,
     lossCode,lossName,lossDurationSeconds,shiftId,operatorId,partNumber,partName,Boolean(alarmActive),
     idleSeconds,runSeconds,downtimeSeconds,availabilityPermille,performancePermille,qualityPermille,
     oeePermille,wifiRssi,freeHeap,firmwareVersion,eventTime]
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
        `INSERT INTO downtime_events(machine_id,loss_code,category,started_at,note)
         VALUES($1,$2,$3,COALESCE($4::timestamptz,NOW()),$5) RETURNING *`,
        [machineId, code, normalizedStatus, eventTime, lossName || null]
      );
      broadcast?.('downtime', normalizeDowntime(created.rows[0]));
    }
  } else if (normalizedStatus === 'RUNNING') {
    const closed = await pool.query(
      `UPDATE downtime_events
       SET status='CLOSED',closed_at=COALESCE($2::timestamptz,NOW())
       WHERE machine_id=$1 AND status IN ('OPEN','ACKNOWLEDGED') RETURNING *`,
      [machineId, eventTime]
    );
    for (const row of closed.rows) broadcast?.('downtime', normalizeDowntime(row));
  }

  broadcast?.('telemetry', telemetry);
  return telemetry;
}
