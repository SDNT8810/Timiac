CREATE TABLE IF NOT EXISTS diagnostic_events (
  event_key TEXT PRIMARY KEY, device_id TEXT NOT NULL, boot TEXT NOT NULL,
  uptime_ms INTEGER NOT NULL, utc_seconds INTEGER NOT NULL,
  message TEXT NOT NULL, persistent INTEGER NOT NULL DEFAULT 0, received_at INTEGER NOT NULL
);
CREATE INDEX IF NOT EXISTS diagnostic_received ON diagnostic_events(received_at);
CREATE INDEX IF NOT EXISTS diagnostic_device ON diagnostic_events(device_id,received_at);
