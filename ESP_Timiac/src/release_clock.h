#pragma once
// Runs on the master, independently of browser control leases and SD cards.
void releaseClockTick() {
#if TIMIAC_RELEASE
  if (!releaseClockEnabled || !rtcValid || !bothHealthy() || operation.barrier.active() || program.reserved()) return;
  if ((boards[0].homed & kHomedMask) != kHomedMask || (boards[1].homed & kHomedMask) != kHomedMask) return;
  const unsigned minute = rtcTime.hour() * 60 + rtcTime.minute();
  if (!timiac::clockPoseDue(armTimeMinutes, minute)) return;
  timiac::Step step;
  timiac::userAction("show-time", minute / 60, minute % 60, kTravelMm, kMaxFeed, step);
  DynamicJsonDocument doc(2048);
  if (!writePresetStep(step, doc.to<JsonVariant>())) { stopAll("Clock pose validation failed"); return; }
  startOperation(doc["a"], doc["b"], bootId + "-clock-" + String(++sequence), commandHash(doc["a"], doc["b"]));
  if (!systemLatched) armTimeMinutes = minute;
#endif
}
