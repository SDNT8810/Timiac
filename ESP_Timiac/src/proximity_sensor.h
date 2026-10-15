#pragma once
// E3F-DS30B2 NPN NC through the 4.7k/68k/10k interface for a fixed 24 V supply.
// Poll one digital input; there is no pulse timing, interrupt, or distance value.
bool proximitySampled = false, proximityRawHigh = false, proximityDetected = false;
uint32_t proximitySampleAt = 0, proximityTriggers = 0;
timiac::ProximityGate proximityGate;

bool proximityReady() {
  return TIMIAC_PROXIMITY_ENABLED && (!TIMIAC_RELEASE || releaseClockEnabled) &&
    !systemLatched && !stopPending && !stopRequested && !networkChanging && bothHealthy() && !operation.barrier.active() && !program.reserved() &&
    program.state != "ready" && (boards[0].homed & kHomedMask) == kHomedMask && (boards[1].homed & kHomedMask) == kHomedMask;
}
void configureProximity() {
  proximityGate.reset(); proximitySampled = false;
#if TIMIAC_PROXIMITY_ENABLED
  static_assert(TIMIAC_PROXIMITY_PIN != TIMIAC_RTC_SDA && TIMIAC_PROXIMITY_PIN != TIMIAC_RTC_SCL,
                "RTC and photoelectric input pins must be different");
  static_assert(TIMIAC_PROXIMITY_PIN != 18 && TIMIAC_PROXIMITY_PIN != 19,
                "Keep the existing ultrasonic PCB pins unused; use the new GPIO27 input");
  static_assert(TIMIAC_PROXIMITY_ACTIVE_HIGH == 0 || TIMIAC_PROXIMITY_ACTIVE_HIGH == 1,
                "Photoelectric input polarity must be 0 or 1");
  pinMode(TIMIAC_PROXIMITY_PIN, INPUT); // Divider supplies both levels; no internal pull-up.
#endif
}
void proximityTick() {
#if TIMIAC_PROXIMITY_ENABLED
  bool ready = proximityReady();
  if (!ready) proximityGate.reset();
  uint32_t now = millis();
  if (proximitySampled && uint32_t(now - proximitySampleAt) < 20) return;
  // A long gap cannot establish continuous clear/detect time.
  if (proximitySampled && uint32_t(now - proximitySampleAt) > 250) proximityGate.reset();
  proximitySampled = true; proximitySampleAt = now;
  proximityRawHigh = digitalRead(TIMIAC_PROXIMITY_PIN) == HIGH;
  proximityDetected = proximityRawHigh == bool(TIMIAC_PROXIMITY_ACTIVE_HIGH);
  if (proximityGate.sample(proximityDetected, ready, now)) {
    String id = bootId + "-gesture-" + String(++sequence);
    const char* error = startBuiltinPreset(timiac::Preset::Curious, 0, id, hashText("gesture:curious"));
    if (error) logMessage("Gesture ignored: " + String(error));
    else { ++proximityTriggers; logMessage("Photoelectric hand detected: curious creature"); }
  }
#endif
}
void writeProximityStatus(JsonObject doc) {
  bool fresh = proximitySampled && uint32_t(millis() - proximitySampleAt) < 1000;
  doc["enabled"] = bool(TIMIAC_PROXIMITY_ENABLED);
  doc["type"] = "photoelectric"; doc["model"] = "E3F-DS30B2";
  doc["pin"] = TIMIAC_PROXIMITY_PIN; doc["activeHigh"] = bool(TIMIAC_PROXIMITY_ACTIVE_HIGH);
  doc["armed"] = proximityReady() && proximityGate.armed;
  doc["ready"] = proximityReady(); doc["rangeMaxCm"] = 30; // Nominal rating, not a reading.
  if (fresh) { doc["detected"] = proximityDetected; doc["rawHigh"] = proximityRawHigh; }
  else { doc["detected"] = nullptr; doc["rawHigh"] = nullptr; }
  doc["distanceCm"] = nullptr; doc["thresholdCm"] = nullptr;
  doc["triggers"] = proximityTriggers;
}
