-- Apply once to existing databases before deploying the progress-aware worker.
ALTER TABLE ota_jobs ADD COLUMN delivered_at INTEGER;
ALTER TABLE ota_jobs ADD COLUMN progress_done INTEGER NOT NULL DEFAULT 0;
ALTER TABLE ota_jobs ADD COLUMN progress_state TEXT NOT NULL DEFAULT '';
