import crypto from 'node:crypto';
import express from 'express';
import { requireAuth } from './auth.js';

const keyHash = (value) => crypto.createHash('sha256').update(value).digest('hex');
const DEFAULT_CONFIG = { communication: { afmsWeb: { enabled:true,mode:'HYBRID',intervalSeconds:60,heartbeatSeconds:60,productionMilestone:10,sendOnStatusChange:true,sendOnLossChange:true,sendOnShiftChange:true }, googleSheets:{enabled:true,uploadIntervalSeconds:3600,sendBreakdownImmediately:true}, configSync:{enabled:true,checkIntervalSeconds:300} } };

export async function ensureDeviceSchema(pool) {
  await pool.query(`
    CREATE TABLE IF NOT EXISTS afms_devices (id BIGSERIAL PRIMARY KEY,machine_id TEXT NOT NULL UNIQUE,device_id TEXT NOT NULL UNIQUE,api_key_hash TEXT,active BOOLEAN NOT NULL DEFAULT TRUE,config_version INTEGER NOT NULL DEFAULT 1,configuration JSONB NOT NULL DEFAULT '{}'::jsonb,last_config_sync_at TIMESTAMPTZ,last_seen_at TIMESTAMPTZ,firmware_version TEXT,created_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),updated_at TIMESTAMPTZ NOT NULL DEFAULT NOW());
    ALTER TABLE afms_devices ADD COLUMN IF NOT EXISTS api_key_hash TEXT;
    CREATE TABLE IF NOT EXISTS device_config_history (id BIGSERIAL PRIMARY KEY,device_id BIGINT NOT NULL REFERENCES afms_devices(id) ON DELETE CASCADE,config_version INTEGER NOT NULL,configuration JSONB NOT NULL,changed_by TEXT,created_at TIMESTAMPTZ NOT NULL DEFAULT NOW());
    CREATE TABLE IF NOT EXISTS device_events (event_id TEXT PRIMARY KEY,device_id BIGINT NOT NULL REFERENCES afms_devices(id) ON DELETE CASCADE,received_at TIMESTAMPTZ NOT NULL DEFAULT NOW());
  `);
}

function validateConfiguration(config) {
  const web=config?.communication?.afmsWeb||{},google=config?.communication?.googleSheets||{},sync=config?.communication?.configSync||{};
  if(!['INTERVAL','CYCLE','HYBRID'].includes(web.mode||'HYBRID'))return'Invalid AFMS Web mode';
  if(web.intervalSeconds!=null&&(web.intervalSeconds<5||web.intervalSeconds>3600))return'AFMS interval must be 5-3600 seconds';
  if(web.heartbeatSeconds!=null&&(web.heartbeatSeconds<15||web.heartbeatSeconds>3600))return'Heartbeat must be 15-3600 seconds';
  if(web.productionMilestone!=null&&(web.productionMilestone<1||web.productionMilestone>10000))return'Production milestone must be 1-10000';
  if(google.uploadIntervalSeconds!=null&&(google.uploadIntervalSeconds<60||google.uploadIntervalSeconds>86400))return'Google interval must be 60-86400 seconds';
  if(sync.checkIntervalSeconds!=null&&(sync.checkIntervalSeconds<60||sync.checkIntervalSeconds>86400))return'Config sync interval must be 60-86400 seconds';
  return null;
}

async function authenticateDevice(pool, req, res, next) {
  try {
    const supplied=req.get('x-afms-device-key')||'';
    if(!supplied)return res.status(401).json({error:'Device key required'});
    const r=await pool.query(`SELECT id,machine_id AS "machineId",device_id AS "deviceId",active,api_key_hash AS "apiKeyHash" FROM afms_devices WHERE api_key_hash=$1 AND active=TRUE`,[keyHash(supplied)]);
    if(!r.rowCount)return res.status(401).json({error:'Invalid or disabled device key'});
    req.device=r.rows[0]; next();
  } catch(e){next(e);}
}

function validateTelemetry(body, machineId) {
  const statuses=['RUNNING','IDLE','SETUP','STOPPED','BREAKDOWN','OFFLINE'];
  if(body.machineId!==machineId)return'Device key is not assigned to this machine';
  if(!statuses.includes(String(body.status||'').toUpperCase()))return'Invalid machine status';
  for(const name of ['productionCount','rejectCount','cycleTimeMs'])if(body[name]!=null&&(!Number.isInteger(body[name])||body[name]<0))return`${name} must be a non-negative integer`;
  if(body.rejectCount>body.productionCount)return'rejectCount cannot exceed productionCount';
  return null;
}

export function createDevicesRouter(pool) {
  const router=express.Router();
  router.get('/',requireAuth(pool,['ADMIN']),async(_req,res,next)=>{try{const r=await pool.query(`SELECT id,machine_id AS "machineId",device_id AS "deviceId",active,config_version AS "configVersion",configuration,last_config_sync_at AS "lastConfigSyncAt",last_seen_at AS "lastSeenAt",firmware_version AS "firmwareVersion" FROM afms_devices ORDER BY machine_id`);res.json(r.rows);}catch(e){next(e);}});
  router.post('/',requireAuth(pool,['ADMIN']),async(req,res,next)=>{try{const{machineId,deviceId,configuration=DEFAULT_CONFIG}=req.body;if(!machineId||!deviceId)return res.status(400).json({error:'machineId and deviceId are required'});const invalid=validateConfiguration(configuration);if(invalid)return res.status(400).json({error:invalid});const apiKey=crypto.randomBytes(32).toString('base64url');const r=await pool.query(`INSERT INTO afms_devices(machine_id,device_id,api_key_hash,configuration)VALUES($1,$2,$3,$4)RETURNING id,machine_id AS "machineId",device_id AS "deviceId",active,config_version AS "configVersion",configuration`,[machineId,deviceId,keyHash(apiKey),configuration]);await pool.query(`INSERT INTO device_config_history(device_id,config_version,configuration,changed_by)VALUES($1,1,$2,$3)`,[r.rows[0].id,configuration,req.user.username]);res.status(201).json({...r.rows[0],apiKey,warning:'Store this key now. It cannot be retrieved later.'});}catch(e){if(e.code==='23505')return res.status(409).json({error:'Machine ID or device ID already exists'});next(e);}});
  router.post('/:machineId/rotate-key',requireAuth(pool,['ADMIN']),async(req,res,next)=>{try{const apiKey=crypto.randomBytes(32).toString('base64url');const r=await pool.query(`UPDATE afms_devices SET api_key_hash=$2,updated_at=NOW() WHERE machine_id=$1 RETURNING machine_id AS "machineId"`,[req.params.machineId,keyHash(apiKey)]);if(!r.rowCount)return res.status(404).json({error:'Machine not found'});res.json({...r.rows[0],apiKey,warning:'Previous key is now invalid.'});}catch(e){next(e);}});
  router.patch('/:machineId',requireAuth(pool,['ADMIN']),async(req,res,next)=>{try{const active=typeof req.body.active==='boolean'?req.body.active:null;const r=await pool.query(`UPDATE afms_devices SET active=COALESCE($2,active),updated_at=NOW() WHERE machine_id=$1 RETURNING machine_id AS "machineId",active`,[req.params.machineId,active]);if(!r.rowCount)return res.status(404).json({error:'Machine not found'});res.json(r.rows[0]);}catch(e){next(e);}});
  router.put('/:machineId/config',requireAuth(pool,['ADMIN']),async(req,res,next)=>{try{const invalid=validateConfiguration(req.body);if(invalid)return res.status(400).json({error:invalid});const r=await pool.query(`UPDATE afms_devices SET configuration=$2,config_version=config_version+1,updated_at=NOW()WHERE machine_id=$1 RETURNING id,machine_id AS "machineId",config_version AS "configVersion",configuration`,[req.params.machineId,req.body]);if(!r.rowCount)return res.status(404).json({error:'Machine not found'});await pool.query(`INSERT INTO device_config_history(device_id,config_version,configuration,changed_by)VALUES($1,$2,$3,$4)`,[r.rows[0].id,r.rows[0].configVersion,r.rows[0].configuration,req.user.username]);res.json(r.rows[0]);}catch(e){next(e);}});
  router.get('/:machineId/config',authenticateDevice.bind(null,pool),async(req,res,next)=>{try{if(req.device.machineId!==req.params.machineId)return res.status(403).json({error:'Device key does not match machine'});const current=Number(req.query.currentVersion||0);const r=await pool.query(`SELECT machine_id AS "machineId",config_version AS "configVersion",configuration,updated_at AS "updatedAt" FROM afms_devices WHERE id=$1`,[req.device.id]);if(current===r.rows[0].configVersion){res.status(304).end();return;}await pool.query(`UPDATE afms_devices SET last_config_sync_at=NOW()WHERE id=$1`,[req.device.id]);res.json(r.rows[0]);}catch(e){next(e);}});
  router.post('/telemetry',authenticateDevice.bind(null,pool),async(req,res,next)=>{try{const invalid=validateTelemetry(req.body,req.device.machineId);if(invalid)return res.status(400).json({error:invalid});const{eventId,machineId,status,productionCount=0,rejectCount=0,cycleTimeMs=0,lossCode=null,recordedAt,firmwareVersion}=req.body;if(!eventId)return res.status(400).json({error:'eventId is required'});const claim=await pool.query(`INSERT INTO device_events(event_id,device_id)VALUES($1,$2)ON CONFLICT DO NOTHING RETURNING event_id`,[eventId,req.device.id]);if(!claim.rowCount)return res.status(200).json({duplicate:true,eventId});const result=await pool.query(`INSERT INTO machine_telemetry(machine_id,status,production_count,reject_count,cycle_time_ms,loss_code,recorded_at)VALUES($1,$2,$3,$4,$5,$6,COALESCE($7::timestamptz,NOW()))RETURNING id,machine_id AS "machineId",status,production_count AS "productionCount",reject_count AS "rejectCount",cycle_time_ms AS "cycleTimeMs",loss_code AS "lossCode",recorded_at AS "recordedAt"`,[machineId,String(status).toUpperCase(),productionCount,rejectCount,cycleTimeMs,lossCode,recordedAt||null]);await pool.query(`UPDATE afms_devices SET last_seen_at=NOW(),firmware_version=COALESCE($2,firmware_version)WHERE id=$1`,[req.device.id,firmwareVersion||null]);res.status(201).json({...result.rows[0],eventId});}catch(e){next(e);}});
  return router;
}
