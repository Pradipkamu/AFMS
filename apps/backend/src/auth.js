import crypto from 'node:crypto';
import { promisify } from 'node:util';
import express from 'express';

const scrypt = promisify(crypto.scrypt);
const ROLES = ['ADMIN', 'SUPERVISOR', 'MAINTENANCE', 'OPERATOR', 'VIEWER'];
const tokenHash = (token) => crypto.createHash('sha256').update(token).digest('hex');

async function passwordHash(password, salt = crypto.randomBytes(16).toString('hex')) {
  const derived = await scrypt(password, salt, 64);
  return `${salt}:${Buffer.from(derived).toString('hex')}`;
}
async function passwordMatches(password, stored) {
  const [salt, expectedHex] = String(stored || '').split(':');
  if (!salt || !expectedHex) return false;
  const derived = Buffer.from(await scrypt(password, salt, 64));
  const expected = Buffer.from(expectedHex, 'hex');
  return derived.length === expected.length && crypto.timingSafeEqual(derived, expected);
}

export async function ensureAuthSchema(pool) {
  await pool.query(`
    CREATE TABLE IF NOT EXISTS app_users (
      id BIGSERIAL PRIMARY KEY,
      username TEXT NOT NULL UNIQUE,
      display_name TEXT NOT NULL,
      role TEXT NOT NULL CHECK (role IN ('ADMIN','SUPERVISOR','MAINTENANCE','OPERATOR','VIEWER')),
      password_hash TEXT NOT NULL,
      active BOOLEAN NOT NULL DEFAULT TRUE,
      created_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
      updated_at TIMESTAMPTZ NOT NULL DEFAULT NOW()
    );
    CREATE TABLE IF NOT EXISTS user_sessions (
      id BIGSERIAL PRIMARY KEY,
      user_id BIGINT NOT NULL REFERENCES app_users(id) ON DELETE CASCADE,
      token_hash TEXT NOT NULL UNIQUE,
      expires_at TIMESTAMPTZ NOT NULL,
      created_at TIMESTAMPTZ NOT NULL DEFAULT NOW()
    );
    CREATE INDEX IF NOT EXISTS idx_user_sessions_token ON user_sessions(token_hash, expires_at);
  `);

  const adminPassword = process.env.AFMS_ADMIN_PASSWORD;
  if (adminPassword) {
    const username = process.env.AFMS_ADMIN_USERNAME || 'admin';
    const existing = await pool.query('SELECT id FROM app_users WHERE username=$1', [username]);
    if (!existing.rowCount) {
      const hash = await passwordHash(adminPassword);
      await pool.query('INSERT INTO app_users(username,display_name,role,password_hash) VALUES($1,$2,$3,$4)', [username, 'AFMS Administrator', 'ADMIN', hash]);
      console.log(`AFMS bootstrap administrator created: ${username}`);
    }
  }
  await pool.query('DELETE FROM user_sessions WHERE expires_at < NOW()');
}

async function authenticatedUser(pool, req) {
  const authorization = req.get('authorization') || '';
  const bearer = authorization.startsWith('Bearer ') ? authorization.slice(7) : '';
  const token = bearer || req.get('x-afms-token') || '';
  if (!token) return null;
  const result = await pool.query(`SELECT u.id,u.username,u.display_name AS "displayName",u.role,u.active,s.expires_at AS "expiresAt" FROM user_sessions s JOIN app_users u ON u.id=s.user_id WHERE s.token_hash=$1 AND s.expires_at>NOW() AND u.active=TRUE`, [tokenHash(token)]);
  return result.rows[0] || null;
}

export function requireAuth(pool, roles = []) {
  return async (req, res, next) => {
    try {
      const user = await authenticatedUser(pool, req);
      if (!user) return res.status(401).json({ error: 'Authentication required' });
      if (roles.length && !roles.includes(user.role)) return res.status(403).json({ error: 'Insufficient permission' });
      req.user = user;
      next();
    } catch (error) { next(error); }
  };
}

export function createAuthRouter(pool) {
  const router = express.Router();

  router.post('/login', async (req, res, next) => {
    try {
      const { username, password } = req.body;
      if (!username || !password) return res.status(400).json({ error: 'username and password are required' });
      const result = await pool.query('SELECT * FROM app_users WHERE username=$1 AND active=TRUE', [username]);
      const user = result.rows[0];
      if (!user || !(await passwordMatches(password, user.password_hash))) return res.status(401).json({ error: 'Invalid username or password' });
      const token = crypto.randomBytes(32).toString('base64url');
      const expiresAt = new Date(Date.now() + 12 * 3600_000);
      await pool.query('INSERT INTO user_sessions(user_id,token_hash,expires_at) VALUES($1,$2,$3)', [user.id, tokenHash(token), expiresAt]);
      res.json({ token, expiresAt, user: { id: user.id, username: user.username, displayName: user.display_name, role: user.role } });
    } catch (error) { next(error); }
  });

  router.get('/me', requireAuth(pool), (req, res) => res.json(req.user));
  router.post('/logout', requireAuth(pool), async (req, res, next) => {
    try { const token=(req.get('authorization')||'').replace(/^Bearer /,'')||req.get('x-afms-token')||''; await pool.query('DELETE FROM user_sessions WHERE token_hash=$1',[tokenHash(token)]); res.status(204).end(); } catch(error){ next(error); }
  });

  router.get('/users', requireAuth(pool, ['ADMIN']), async (_req, res, next) => {
    try { const result=await pool.query('SELECT id,username,display_name AS "displayName",role,active,created_at AS "createdAt" FROM app_users ORDER BY username'); res.json(result.rows); } catch(error){ next(error); }
  });
  router.post('/users', requireAuth(pool, ['ADMIN']), async (req, res, next) => {
    try {
      const { username, displayName, role='VIEWER', password } = req.body;
      if (!username || !displayName || !password || !ROLES.includes(role)) return res.status(400).json({ error: 'Valid username, displayName, role and password are required' });
      const hash=await passwordHash(password);
      const result=await pool.query('INSERT INTO app_users(username,display_name,role,password_hash) VALUES($1,$2,$3,$4) RETURNING id,username,display_name AS "displayName",role,active,created_at AS "createdAt"',[username,displayName,role,hash]);
      res.status(201).json(result.rows[0]);
    } catch(error){ if(error.code==='23505') return res.status(409).json({error:'Username already exists'}); next(error); }
  });
  router.patch('/users/:id', requireAuth(pool, ['ADMIN']), async (req, res, next) => {
    try {
      const { displayName, role, active, password }=req.body;
      if (role && !ROLES.includes(role)) return res.status(400).json({error:'Invalid role'});
      const hash=password?await passwordHash(password):null;
      const result=await pool.query(`UPDATE app_users SET display_name=COALESCE($2,display_name),role=COALESCE($3,role),active=COALESCE($4,active),password_hash=COALESCE($5,password_hash),updated_at=NOW() WHERE id=$1 RETURNING id,username,display_name AS "displayName",role,active`,[req.params.id,displayName||null,role||null,typeof active==='boolean'?active:null,hash]);
      if(!result.rowCount)return res.status(404).json({error:'User not found'});res.json(result.rows[0]);
    } catch(error){next(error);}
  });

  return router;
}
