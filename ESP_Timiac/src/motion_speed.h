#pragma once

Preferences motionPreferences;
bool motionStorageReady = false;
void configureMotionSpeed() {
  motionStorageReady = motionPreferences.begin("timiac-motion", false);
  if (!motionStorageReady) return;
  const unsigned saved = motionPreferences.getUInt("speed", timiac::defaultSpeedPercent);
  if (timiac::validSpeedPercent(saved)) motionSpeedPercent = saved;
}
const char* saveMotionSpeed(JsonVariant doc) {
  if (!doc["percent"].is<unsigned>() || !timiac::validSpeedPercent(doc["percent"].as<unsigned>()))
    return "Speed must be a whole percentage from 20 to 100";
  if (operation.barrier.active() || program.reserved() || boards[0].busy || boards[1].busy || stopPending || stopRequested)
    return "Wait for the current movement to finish before changing speed";
  const unsigned percent = doc["percent"].as<unsigned>();
  if (!motionStorageReady || (percent != motionSpeedPercent && !motionPreferences.putUInt("speed", percent)))
    return "Could not save movement speed";
  motionSpeedPercent = percent;
  return nullptr;
}
