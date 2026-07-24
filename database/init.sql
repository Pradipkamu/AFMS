CREATE TABLE IF NOT EXISTS machine_telemetry (
  id BIGSERIAL PRIMARY KEY,
  machine_id TEXT NOT NULL,
  status TEXT NOT NULL,
  production_count INTEGER NOT NULL DEFAULT 0 CHECK (production_count >= 0),
  reject_count INTEGER NOT NULL DEFAULT 0 CHECK (reject_count >= 0),
  cycle_time_ms INTEGER NOT NULL DEFAULT 0 CHECK (cycle_time_ms >= 0),
  loss_code TEXT,
  recorded_at TIMESTAMPTZ NOT NULL DEFAULT NOW()
);

CREATE INDEX IF NOT EXISTS idx_machine_telemetry_machine_time
  ON machine_telemetry (machine_id, recorded_at DESC);
