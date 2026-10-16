#pragma once
// Runs on the master, independently of browser control leases and SD cards.
void releaseClockTick() {
#if TIMIAC_RELEASE
  if (!releaseClockEnabled) releaseClockMinute = -1;
  if (!releaseStartupPending && !releaseClockEnabled) return;
  if (!bothHealthy() || operation.barrier.active() || program.reserved() || boards[0].busy || boards[1].busy) return;
  if (releaseStartupPending) {
    // One attempt per master boot. Stop/failure cancels it; reconnects never rearm it.
    releaseStartupPending = false;
    timiac::Step home;
    timiac::userAction("home", 0, 0, kTravelMm, kMaxFeed, home);
    DynamicJsonDocument doc(2048);
    if (!writePresetStep(home, doc.to<JsonVariant>())) { stopAll("Startup homing validation failed"); return; }
    armTimeMinutes = -1;
    releaseClockMinute = -1;
    releaseClockEnabled = true;
    clockPauseReason = "";
    logMessage("Release startup: homing all twelve arms, then displaying current time");
    startOperation(doc["a"], doc["b"], bootId + "-startup-home-" + String(++sequence), commandHash(doc["a"], doc["b"]));
    return;
  }
  // Homing can run without an RTC; the clock pose waits for valid time and both completions.
  if (!releaseClockEnabled || !rtcValid) return;
  if ((boards[0].homed & kHomedMask) != kHomedMask || (boards[1].homed & kHomedMask) != kHomedMask) return;
  const unsigned minute = rtcTime.hour() * 60 + rtcTime.minute();
  // Start clock may already have sent the correct pose through the API.
  // Establish tracking now even if no extra five-minute move is necessary.
  if (releaseClockMinute < 0 && !timiac::clockPoseDue(armTimeMinutes, minute)) releaseClockMinute = minute;
  if (timiac::clockHourDue(releaseClockMinute, minute)) {
    const String id = bootId + "-hour-" + String(++sequence);
    const char* error = startBuiltinPreset(timiac::Preset::Hour, minute, id, hashText(id));
    if (error) { stopAll("Automatic hour dance: " + String(error)); return; }
    releaseClockMinute = minute; // Claim this boundary before the animation starts.
    logMessage("Hour changed: dancing to the current time");
    return;
  }
  if (!timiac::clockPoseDue(armTimeMinutes, minute)) return;
  timiac::Step step;
  timiac::userAction("show-time", minute / 60, minute % 60, kTravelMm, kMaxFeed, step);
  DynamicJsonDocument doc(2048);
  if (!writePresetStep(step, doc.to<JsonVariant>())) { stopAll("Clock pose validation failed"); return; }
  startOperation(doc["a"], doc["b"], bootId + "-clock-" + String(++sequence), commandHash(doc["a"], doc["b"]));
  if (!systemLatched) armTimeMinutes = releaseClockMinute = minute;
#endif
}
