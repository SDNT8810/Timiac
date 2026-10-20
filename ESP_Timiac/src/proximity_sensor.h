#pragma once
// Read the installed low-voltage interface through ADC1, without changing its
// resistors. These are presence readings, not a distance measurement.
bool proximitySampled = false, proximityAdcReady = false, proximityValid = false;
bool proximityKnown = false, proximityDetected = false;
uint16_t proximityRawAdc = 0;
uint32_t proximityMillivolts = 0, proximityFloorMillivolts = 0;
uint32_t proximitySampleAt = 0, proximityTriggers = 0;
timiac::ProximityGate proximityGate;
timiac::ProximityObservation proximityObservation;
uint32_t proximityIgnored = 0, proximityLastEventAt = 0, proximityLastTriggerAt = 0, proximityLastIgnoredLogAt = 0;
String proximityLastOutcome, proximityLastReason, proximityLastEventTime, proximityLastTriggerTime, proximityLastTriggerId;
String proximityLastLoggedReason;

const char* proximityMotionBlockReason() {
  if (!TIMIAC_PROXIMITY_ENABLED) return "Sensor disabled";
  if (firmwareUpdating) return "Firmware update in progress";
  if (!rtcValid) return "Clock time is invalid";
  if (TIMIAC_RELEASE && !releaseClockEnabled) return "Clock paused; start the clock first";
  if (systemLatched) return "Controller fault is active";
  if (stopPending || stopRequested) return "Stop is active";
  if (networkChanging) return "Wi-Fi configuration is changing";
  if (!bothHealthy()) return "Motor boards are not ready";
  if (operation.barrier.active() || program.reserved() || program.state == "ready") return "Motion or a program is busy";
  if ((boards[0].homed & kHomedMask) != kHomedMask || (boards[1].homed & kHomedMask) != kHomedMask) return "Arms need homing";
  return nullptr;
}
bool proximityReady() { return !proximityMotionBlockReason(); }
void recordProximityEvent(bool triggered, const String& reason) {
  proximityLastEventAt = millis(); proximityLastEventTime = rtcValid ? rtcText : String();
  proximityLastOutcome = triggered ? "triggered" : "ignored"; proximityLastReason = reason.substring(0, 96);
  if (triggered) {
    ++proximityTriggers; proximityLastTriggerAt = proximityLastEventAt;
    proximityLastTriggerTime = proximityLastEventTime; proximityLastTriggerId = program.id;
  } else ++proximityIgnored;
  // Update live history for every arrival, but do not repeatedly write the
  // same ignored reason to flash during a noisy commissioning session.
  if (triggered || proximityLastLoggedReason != reason || !proximityLastIgnoredLogAt ||
      uint32_t(millis() - proximityLastIgnoredLogAt) >= 30000) {
    logMessage("Hand sensor: " + String(triggered ? "Struggle triggered" : "ignored: " + reason) +
      (proximityLastEventTime.isEmpty() ? String() : " at " + proximityLastEventTime) +
      "; ADC=" + String(proximityRawAdc) + (proximityRawAdc ? " (~" + String(proximityMillivolts) + "mV)" : " (below ADC floor)"));
    if (!triggered) { proximityLastLoggedReason = reason; proximityLastIgnoredLogAt = millis(); }
  }
}
void configureProximity() {
  proximityGate.reset(); proximitySampled = proximityValid = proximityKnown = proximityDetected = false;
  proximityAdcReady = false;
  proximityObservation.reset(); proximityTriggers = proximityIgnored = proximityLastEventAt = proximityLastTriggerAt = proximityLastIgnoredLogAt = 0;
  proximityLastOutcome = proximityLastReason = proximityLastEventTime = proximityLastTriggerTime = proximityLastTriggerId = proximityLastLoggedReason = "";
#if TIMIAC_PROXIMITY_ENABLED
  static_assert(TIMIAC_PROXIMITY_PIN != TIMIAC_RTC_SDA && TIMIAC_PROXIMITY_PIN != TIMIAC_RTC_SCL,
                "RTC and photoelectric input pins must be different");
  static_assert(TIMIAC_PROXIMITY_PIN >= 32 && TIMIAC_PROXIMITY_PIN <= 39,
                "Analog proximity requires ADC1 GPIO32..39; ADC2 conflicts with Wi-Fi");
  static_assert(TIMIAC_PROXIMITY_DETECT_MV > 0 && TIMIAC_PROXIMITY_DETECT_MV < TIMIAC_PROXIMITY_CLEAR_MV,
                "Hand threshold must be below the clear threshold");
  pinMode(TIMIAC_PROXIMITY_PIN, INPUT); // No internal pull-up/down.
  proximityAdcReady = configureProximityAdc(TIMIAC_PROXIMITY_PIN);
  if (!proximityAdcReady) logMessage("Hand sensor ADC1 configuration failed; gestures disabled");
#endif
}
void proximityTick() {
#if TIMIAC_PROXIMITY_ENABLED
  bool ready = proximityReady();
  if (!ready) proximityGate.reset();
  uint32_t now = millis();
  if (proximitySampled && uint32_t(now - proximitySampleAt) < 20) return;
  // A long gap cannot establish continuous clear/detect time.
  if (proximitySampled && uint32_t(now - proximitySampleAt) > 250) {
    proximityGate.reset(); proximityKnown = false; proximityObservation.samplingGap();
  }
  proximitySampled = true; proximitySampleAt = now;
  proximityValid = proximityAdcReady && readProximityAdc(proximityRawAdc, proximityMillivolts, proximityFloorMillivolts);
  if (!proximityValid) { proximityGate.reset(); proximityKnown = false; proximityObservation.samplingGap(); return; }
  // The classic ESP32 ADC clips near ground; its calibrated API can return
  // ~75 mV for zero counts. Treat a real zero-count input as the low state,
  // but report its voltage as unknown/below the ADC floor, never as exact 0 V.
  if (!proximityRawAdc || proximityMillivolts < TIMIAC_PROXIMITY_DETECT_MV) {
    proximityDetected = true; proximityKnown = true;
  } else if (proximityMillivolts > TIMIAC_PROXIMITY_CLEAR_MV) {
    proximityDetected = false; proximityKnown = true;
  }
  if (!proximityKnown) { proximityGate.reset(); proximityObservation.samplingGap(); return; }
  const bool arrival = proximityObservation.sample(proximityDetected, now);
  if (proximityGate.sample(proximityDetected, ready, now)) {
    String id = bootId + "-gesture-" + String(++sequence);
    const unsigned minute = rtcTime.hour() * 60 + rtcTime.minute();
    const char* error = startBuiltinPreset(timiac::Preset::Struggle, minute, id, hashText("gesture:struggle"));
    recordProximityEvent(!error, error ? String(error) : String());
  } else if (arrival) {
    const char* reason = proximityMotionBlockReason();
    recordProximityEvent(false, reason ? reason : "Sensor was not armed; clear it for 500 ms first");
  }
#endif
}
void writeProximityStatus(JsonObject doc) {
  bool fresh = proximitySampled && proximityValid && uint32_t(millis() - proximitySampleAt) < 1000;
  doc["enabled"] = bool(TIMIAC_PROXIMITY_ENABLED);
  doc["type"] = "photoelectric"; doc["model"] = "E3F-DS30B2";
  doc["pin"] = TIMIAC_PROXIMITY_PIN; doc["activeHigh"] = false; doc["inputMode"] = "analog";
  doc["armed"] = fresh && proximityKnown && proximityReady() && proximityGate.armed;
  doc["ready"] = fresh && proximityKnown && proximityReady(); doc["rangeMaxCm"] = 30;
  doc["detectBelowMv"] = TIMIAC_PROXIMITY_DETECT_MV; doc["clearAboveMv"] = TIMIAC_PROXIMITY_CLEAR_MV;
  if (fresh) {
    doc["rawAdc"] = proximityRawAdc; doc["adcFloorMv"] = proximityFloorMillivolts;
    doc["belowRange"] = proximityRawAdc == 0 || proximityMillivolts < 100;
    if (proximityRawAdc) doc["millivolts"] = proximityMillivolts; else doc["millivolts"] = nullptr;
  } else { doc["rawAdc"] = nullptr; doc["millivolts"] = nullptr; doc["adcFloorMv"] = nullptr; doc["belowRange"] = nullptr; }
  if (fresh && proximityKnown) doc["detected"] = proximityDetected; else doc["detected"] = nullptr;
  doc["rawHigh"] = nullptr; // Legacy digital-only field.
  const char* blocked = proximityMotionBlockReason();
  doc["blockedReason"] = blocked ? blocked : !fresh ? "Sensor reading is unavailable or stale" :
    !proximityKnown ? "Reading is between thresholds" : !proximityGate.armed ? "Withdraw hand and clear the sensor for 500 ms" : "";
  doc["ignored"] = proximityIgnored; doc["lastOutcome"] = proximityLastOutcome;
  doc["lastReason"] = proximityLastReason; doc["lastEventTime"] = proximityLastEventTime;
  doc["lastTriggerTime"] = proximityLastTriggerTime; doc["lastTriggerId"] = proximityLastTriggerId;
  if (!proximityLastOutcome.isEmpty()) doc["lastEventAgeMs"] = uint32_t(millis() - proximityLastEventAt);
  else doc["lastEventAgeMs"] = nullptr;
  if (proximityTriggers) doc["lastTriggerAgeMs"] = uint32_t(millis() - proximityLastTriggerAt);
  else doc["lastTriggerAgeMs"] = nullptr;
  doc["distanceCm"] = nullptr; doc["thresholdCm"] = nullptr;
  doc["triggers"] = proximityTriggers;
}
