CREATE TABLE IF NOT EXISTS devices (id TEXT PRIMARY KEY, status TEXT NOT NULL, updated_at INTEGER NOT NULL);
CREATE TABLE IF NOT EXISTS actions (
  id TEXT PRIMARY KEY, device_id TEXT NOT NULL, type TEXT NOT NULL,
  local_time TEXT NOT NULL DEFAULT '', percent INTEGER, session TEXT NOT NULL,
  requested_by TEXT NOT NULL, state TEXT NOT NULL DEFAULT 'queued',
  created_at INTEGER NOT NULL, expires_at INTEGER NOT NULL, delivered_at INTEGER
);
CREATE INDEX IF NOT EXISTS actions_queue ON actions(device_id, state, created_at);
CREATE TABLE IF NOT EXISTS login_limits (id TEXT PRIMARY KEY, attempts INTEGER NOT NULL, window_start INTEGER NOT NULL);
