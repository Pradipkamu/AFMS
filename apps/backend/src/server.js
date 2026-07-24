import 'dotenv/config';
import cors from 'cors';
import express from 'express';
import helmet from 'helmet';
import pg from 'pg';
import { createReportsRouter } from './reports.js';
import { createAuthRouter, ensureAuthSchema } from './auth.js';
import { createDevicesRouter, ensureDeviceSchema } from './devices.js';
import { ingestTelemetry, validateTelemetry } from './telemetry.js';

const { Pool } = pg;
const app = express();
const port = Number(process.env.PORT || 3000);
const pool = new Pool({ connectionString: process.env.DATABASE_URL });
const eventClients = new Set();
const asyncHandler = (handler) => (req, res, next) => Promise.resolve(handler(req, res, next)).catch(next);
const clamp = (value, minimum = 0, maximum = 100) => Math.min(maximum, Math.max(minimum, Number(value) || 0));
function sendEvent(res, event, payload) { res.write(`event: ${event}\n`); res.write(`data: ${JSON.stringify(payload)}\n\n`); }
function broadcast(event, payload) { for (const client of eventClients) sendEvent(client, event, payload); }
async function ensureSchema() {
  await pool.query(`
    CREATE TABLE IF NOT EXISTS downtime_events (
      id BIGSERIAL PRIMARY KEY,machine_id TEXT NOT NULL,loss_code TEXT NOT NULL,
      category TEXT NOT NULL DEFAULT 'UNCLASSIFIED',status TEXT NOT NULL DEFAULT 'OPEN'
      CHECK (status IN ('OPEN','ACKNOWLEDGED','CLOSED')),started_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
      acknowledged_at TIMESTAMPTZ,acknowledged_by TEXT,note TEXT,closed_at TIMESTAMPTZ,
      created_at TIMESTAMPTZ NOT NULL DEFAULT NOW()
    );
    CREATE INDEX IF NOT EXISTS idx_downtime_machine_status ON downtime_events (machine_id,status,started_at DESC);
    CREATE INDEX IF NOT EXISTS idx_downtime_started_at ON downtime_events (started_at DESC);

    ALTER TABLE machine_telemetry ADD COLUMN IF NOT EXISTS machine_name TEXT;
    ALTER TABLE machine_telemetry ADD COLUMN IF NOT EXISTS good_count BIGINT NOT NULL DEFAULT 0;
    ALTER TABLE machine_telemetry ADD COLUMN IF NOT EXISTS loss_name TEXT;
    ALTER TABLE machine_telemetry ADD COLUMN IF NOT EXISTS loss_duration_seconds BIGINT NOT NULL DEFAULT 0;
    ALTER TABLE machine_telemetry ADD COLUMN IF NOT EXISTS shift_id BIGINT;
    ALTER TABLE machine_telemetry ADD COLUMN IF NOT EXISTS operator_id BIGINT;
    ALTER TABLE machine_telemetry ADD COLUMN IF NOT EXISTS part_number BIGINT;
    ALTER TABLE machine_telemetry ADD COLUMN IF NOT EXISTS part_name TEXT;
    ALTER TABLE machine_telemetry ADD COLUMN IF NOT EXISTS alarm_active BOOLEAN NOT NULL DEFAULT FALSE;
    ALTER TABLE machine_telemetry ADD COLUMN IF NOT EXISTS idle_seconds BIGINT NOT NULL DEFAULT 0;
    ALTER TABLE machine_telemetry ADD COLUMN IF NOT EXISTS run_seconds BIGINT NOT NULL DEFAULT 0;
    ALTER TABLE machine_telemetry ADD COLUMN IF NOT EXISTS downtime_seconds BIGINT NOT NULL DEFAULT 0;
    ALTER TABLE machine_telemetry ADD COLUMN IF NOT EXISTS availability_permille INTEGER;
    ALTER TABLE machine_telemetry ADD COLUMN IF NOT EXISTS performance_permille INTEGER;
    ALTER TABLE machine_telemetry ADD COLUMN IF NOT EXISTS quality_permille INTEGER;
    ALTER TABLE machine_telemetry ADD COLUMN IF NOT EXISTS oee_permille INTEGER;
    ALTER TABLE machine_telemetry ADD COLUMN IF NOT EXISTS wifi_rssi INTEGER;
    ALTER TABLE machine_telemetry ADD COLUMN IF NOT EXISTS free_heap BIGINT;
    ALTER TABLE machine_telemetry ADD COLUMN IF NOT EXISTS firmware_version TEXT;
    CREATE INDEX IF NOT EXISTS idx_machine_telemetry_machine_recorded ON machine_telemetry(machine_id,recorded_at DESC,id DESC);
  `);
}

app.use(helmet({ crossOriginResourcePolicy: false })); app.use(cors()); app.use(express.json({ limit: '64kb' }));
app.get('/api/health',asyncHandler(async(_req,res)=>{await pool.query('SELECT 1');res.json({status:'ok',service:'afms-backend',database:'connected',liveClients:eventClients.size});}));
app.get('/api/v1/events',(req,res)=>{res.set({'Content-Type':'text/event-stream','Cache-Control':'no-cache, no-transform',Connection:'keep-alive','X-Accel-Buffering':'no'});res.flushHeaders();eventClients.add(res);sendEvent(res,'connected',{connectedAt:new Date().toISOString()});const heartbeat=setInterval(()=>sendEvent(res,'heartbeat',{timestamp:new Date().toISOString()}),20000);req.on('close',()=>{clearInterval(heartbeat);eventClients.delete(res);res.end();});});
app.post('/api/v1/telemetry',asyncHandler(async(req,res)=>{const invalid=validateTelemetry(req.body);if(invalid)return res.status(400).json({error:invalid});const telemetry=await ingestTelemetry(pool,req.body,broadcast);return res.status(201).json(telemetry);}));
app.get('/api/v1/machines/latest',asyncHandler(async(_req,res)=>{const result=await pool.query(`SELECT DISTINCT ON(machine_id)
  machine_id AS "machineId",machine_name AS "machineName",status,
  production_count AS "productionCount",reject_count AS "rejectCount",good_count AS "goodCount",
  cycle_time_ms AS "cycleTimeMs",loss_code AS "lossCode",loss_name AS "lossName",
  loss_duration_seconds AS "lossDurationSeconds",shift_id AS "shiftId",operator_id AS "operatorId",
  part_number AS "partNumber",part_name AS "partName",alarm_active AS "alarmActive",
  idle_seconds AS "idleSeconds",run_seconds AS "runSeconds",downtime_seconds AS "downtimeSeconds",
  availability_permille AS "availabilityPermille",performance_permille AS "performancePermille",
  quality_permille AS "qualityPermille",oee_permille AS "oeePermille",wifi_rssi AS "wifiRssi",
  free_heap AS "freeHeap",firmware_version AS "firmwareVersion",recorded_at AS "recordedAt"
  FROM machine_telemetry ORDER BY machine_id,recorded_at DESC,id DESC`);res.json(result.rows);}));
app.get('/api/v1/downtime',asyncHandler(async(req,res)=>{const status=String(req.query.status||'').toUpperCase(),machineId=String(req.query.machineId||''),limit=Math.min(500,Math.max(1,Number(req.query.limit)||100));const result=await pool.query(`SELECT id,machine_id AS "machineId",loss_code AS "lossCode",category,status,started_at AS "startedAt",acknowledged_at AS "acknowledgedAt",acknowledged_by AS "acknowledgedBy",note,closed_at AS "closedAt",EXTRACT(EPOCH FROM(COALESCE(closed_at,NOW())-started_at))::bigint AS "durationSeconds" FROM downtime_events WHERE($1='' OR status=$1)AND($2='' OR machine_id=$2) ORDER BY started_at DESC LIMIT $3`,[status,machineId,limit]);res.json(result.rows);}));
app.post('/api/v1/downtime',asyncHandler(async(req,res)=>{const{machineId,lossCode,category='MANUAL',startedAt,note=null}=req.body;if(!machineId||!lossCode)return res.status(400).json({error:'machineId and lossCode are required'});const result=await pool.query(`INSERT INTO downtime_events(machine_id,loss_code,category,started_at,note) VALUES($1,$2,$3,COALESCE($4::timestamptz,NOW()),$5) RETURNING *`,[machineId,lossCode,category,startedAt||null,note]);broadcast('downtime',result.rows[0]);res.status(201).json(result.rows[0]);}));
app.post('/api/v1/downtime/:id/acknowledge',asyncHandler(async(req,res)=>{const{acknowledgedBy='Supervisor',note=null,category=null}=req.body;const result=await pool.query(`UPDATE downtime_events SET status='ACKNOWLEDGED',acknowledged_at=NOW(),acknowledged_by=$2,note=COALESCE($3,note),category=COALESCE($4,category) WHERE id=$1 AND status='OPEN' RETURNING *`,[req.params.id,acknowledgedBy,note,category]);if(!result.rowCount)return res.status(404).json({error:'Open downtime event not found'});broadcast('downtime',result.rows[0]);res.json(result.rows[0]);}));
app.post('/api/v1/downtime/:id/close',asyncHandler(async(req,res)=>{const result=await pool.query(`UPDATE downtime_events SET status='CLOSED',closed_at=NOW(),note=COALESCE($2,note) WHERE id=$1 AND status<>'CLOSED' RETURNING *`,[req.params.id,req.body.note||null]);if(!result.rowCount)return res.status(404).json({error:'Active downtime event not found'});broadcast('downtime',result.rows[0]);res.json(result.rows[0]);}));
app.get('/api/v1/oee',asyncHandler(async(req,res)=>{const windowHours=Math.min(168,Math.max(1,Number(req.query.windowHours)||8)),defaultIdealCycleMs=Math.min(3600000,Math.max(1,Number(req.query.idealCycleMs)||12000));const result=await pool.query(`WITH windowed AS(SELECT *,LEAD(recorded_at,1,NOW())OVER(PARTITION BY machine_id ORDER BY recorded_at,id)AS next_at FROM machine_telemetry WHERE recorded_at>=NOW()-($1::text||' hours')::interval),machine_rollup AS(SELECT machine_id,EXTRACT(EPOCH FROM(MAX(recorded_at)-MIN(recorded_at)))AS observed_seconds,SUM(CASE WHEN UPPER(status)='RUNNING'THEN GREATEST(0,EXTRACT(EPOCH FROM(LEAST(next_at,NOW())-recorded_at)))ELSE 0 END)AS running_seconds,GREATEST(0,MAX(production_count)-MIN(production_count))AS total_parts,GREATEST(0,MAX(reject_count)-MIN(reject_count))AS reject_parts,NULLIF(AVG(NULLIF(cycle_time_ms,0)),0)AS average_cycle_ms FROM windowed GROUP BY machine_id)SELECT machine_id AS "machineId",observed_seconds AS "observedSeconds",running_seconds AS "runningSeconds",total_parts AS "totalParts",reject_parts AS "rejectParts",average_cycle_ms AS "averageCycleMs" FROM machine_rollup ORDER BY machine_id`,[windowHours]);const machines=result.rows.map(row=>{const observedSeconds=Number(row.observedSeconds||0),runningSeconds=Number(row.runningSeconds||0),totalParts=Number(row.totalParts||0),rejectParts=Number(row.rejectParts||0),goodParts=Math.max(0,totalParts-rejectParts),idealCycleMs=Number(row.averageCycleMs||defaultIdealCycleMs),availability=observedSeconds>0?clamp(runningSeconds/observedSeconds*100):0,performance=runningSeconds>0?clamp((idealCycleMs/1000)*totalParts/runningSeconds*100):0,quality=totalParts>0?clamp(goodParts/totalParts*100):100,oee=clamp(availability*performance*quality/10000);return{machineId:row.machineId,availability,performance,quality,oee,totalParts,goodParts,rejectParts,runningSeconds,observedSeconds,idealCycleMs};});const weighted=machines.reduce((a,m)=>{const w=Math.max(m.observedSeconds,1);a.weight+=w;a.availability+=m.availability*w;a.performance+=m.performance*w;a.quality+=m.quality*w;a.oee+=m.oee*w;a.totalParts+=m.totalParts;a.goodParts+=m.goodParts;a.rejectParts+=m.rejectParts;return a;},{weight:0,availability:0,performance:0,quality:0,oee:0,totalParts:0,goodParts:0,rejectParts:0});const d=weighted.weight||1;res.json({generatedAt:new Date().toISOString(),windowHours,summary:{availability:weighted.availability/d,performance:weighted.performance/d,quality:weighted.quality/d,oee:weighted.oee/d,totalParts:weighted.totalParts,goodParts:weighted.goodParts,rejectParts:weighted.rejectParts,machines:machines.length},machines});}));
app.use('/api/v1/reports',createReportsRouter(pool));
app.use('/api/v1/auth',createAuthRouter(pool));
app.use('/api/v1/devices',createDevicesRouter(pool,broadcast));
app.use((error,_req,res,_next)=>{console.error(error);if(!res.headersSent)res.status(500).json({error:'Internal server error'});});
await ensureSchema(); await ensureAuthSchema(pool); await ensureDeviceSchema(pool);
const server=app.listen(port,'0.0.0.0',()=>console.log(`AFMS backend listening on port ${port}`));
async function shutdown(signal){console.log(`${signal} received, shutting down AFMS backend`);server.close(async()=>{for(const client of eventClients)client.end();await pool.end();process.exit(0);});}
process.on('SIGTERM',()=>shutdown('SIGTERM'));process.on('SIGINT',()=>shutdown('SIGINT'));
