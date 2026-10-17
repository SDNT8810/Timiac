CREATE TABLE IF NOT EXISTS devices (id TEXT PRIMARY KEY, status TEXT NOT NULL, updated_at INTEGER NOT NULL);
CREATE TABLE IF NOT EXISTS actions (
  id TEXT PRIMARY KEY, device_id TEXT NOT NULL, type TEXT NOT NULL,
  local_time TEXT NOT NULL DEFAULT '', percent INTEGER, session TEXT NOT NULL,
  requested_by TEXT NOT NULL, state TEXT NOT NULL DEFAULT 'queued',
  created_at INTEGER NOT NULL, expires_at INTEGER NOT NULL, delivered_at INTEGER
);
CREATE INDEX IF NOT EXISTS actions_queue ON actions(device_id, state, created_at);
CREATE TABLE IF NOT EXISTS login_limits (id TEXT PRIMARY KEY, attempts INTEGER NOT NULL, window_start INTEGER NOT NULL);
CREATE TABLE IF NOT EXISTS ota_images (
  id TEXT PRIMARY KEY, target TEXT NOT NULL, kind TEXT NOT NULL, size INTEGER NOT NULL,
  sha256 TEXT NOT NULL, revision TEXT NOT NULL, machine TEXT NOT NULL, crc32 INTEGER NOT NULL,
  created_at INTEGER NOT NULL, complete INTEGER NOT NULL DEFAULT 0
);
CREATE TABLE IF NOT EXISTS ota_chunks (
  image_id TEXT NOT NULL REFERENCES ota_images(id) ON DELETE CASCADE,
  offset INTEGER NOT NULL, data BLOB NOT NULL, PRIMARY KEY(image_id, offset)
);
CREATE TABLE IF NOT EXISTS ota_jobs (
  id TEXT PRIMARY KEY, image_id TEXT NOT NULL REFERENCES ota_images(id), device_id TEXT NOT NULL,
  session TEXT NOT NULL, state TEXT NOT NULL, error TEXT NOT NULL DEFAULT '',
  created_at INTEGER NOT NULL, expires_at INTEGER NOT NULL,
  delivered_at INTEGER, progress_done INTEGER NOT NULL DEFAULT 0,
  progress_state TEXT NOT NULL DEFAULT ''
);
CREATE INDEX IF NOT EXISTS ota_queue ON ota_jobs(state, expires_at);

-- One durable build/install sequence; per-step jobs retain the existing OTA receipts.
CREATE TABLE IF NOT EXISTS full_updates (
  id TEXT PRIMARY KEY, device_id TEXT NOT NULL, state TEXT NOT NULL, error TEXT NOT NULL DEFAULT '',
  source_sha TEXT NOT NULL, controller TEXT NOT NULL, validate_only INTEGER NOT NULL DEFAULT 0,
  created_at INTEGER NOT NULL, updated_at INTEGER NOT NULL, expires_at INTEGER NOT NULL
);
CREATE UNIQUE INDEX IF NOT EXISTS full_update_active ON full_updates(device_id) WHERE state IN ('building','installing');
CREATE TABLE IF NOT EXISTS full_update_images (
  full_id TEXT NOT NULL REFERENCES full_updates(id), target TEXT NOT NULL, kind TEXT NOT NULL,
  image_id TEXT NOT NULL REFERENCES ota_images(id), PRIMARY KEY(full_id,target,kind)
);
CREATE TABLE IF NOT EXISTS full_update_steps (
  full_id TEXT NOT NULL REFERENCES full_updates(id), position INTEGER NOT NULL,
  target TEXT NOT NULL, kind TEXT NOT NULL, image_id TEXT NOT NULL REFERENCES ota_images(id),
  job_id TEXT NOT NULL UNIQUE, PRIMARY KEY(full_id,position)
);

CREATE TABLE IF NOT EXISTS diagnostic_events (
  event_key TEXT PRIMARY KEY, device_id TEXT NOT NULL, boot TEXT NOT NULL,
  uptime_ms INTEGER NOT NULL, utc_seconds INTEGER NOT NULL,
  message TEXT NOT NULL, persistent INTEGER NOT NULL DEFAULT 0, received_at INTEGER NOT NULL
);
CREATE INDEX IF NOT EXISTS diagnostic_received ON diagnostic_events(received_at);
CREATE INDEX IF NOT EXISTS diagnostic_device ON diagnostic_events(device_id,received_at);
