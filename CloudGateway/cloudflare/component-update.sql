-- Apply once to existing gateways before deploying single-component updates.
-- Empty selection preserves the existing complete-bundle behavior.
ALTER TABLE full_updates ADD COLUMN selected_target TEXT NOT NULL DEFAULT '';
ALTER TABLE full_updates ADD COLUMN selected_kind TEXT NOT NULL DEFAULT '';
