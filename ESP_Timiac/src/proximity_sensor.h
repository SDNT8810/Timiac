#pragma once
// Only the edge timestamps run in the interrupt; motion runs under RuntimeLock.
portMUX_TYPE sonarMux = portMUX_INITIALIZER_UNLOCKED;
volatile bool sonarCapture = false, sonarRiseSeen = false, sonarDone = false;
volatile uint32_t sonarRise = 0, sonarWidth = 0;
bool sonarWaiting = false;
uint32_t sonarSent = 0, sonarSampleAt = 0, sonarTriggers = 0;
int sonarDistance = -1;
timiac::ProximityGate proximityGate;

void IRAM_ATTR sonarEchoEdge() {
  uint32_t now = micros();
  bool high = digitalRead(TIMIAC_SONAR_ECHO);
  portENTER_CRITICAL_ISR(&sonarMux);
  if (sonarCapture) {
    if (high && !sonarRiseSeen) { sonarRise = now; sonarRiseSeen = true; }
    else if (!high && sonarRiseSeen) {
      sonarWidth = now - sonarRise; sonarDone = true; sonarCapture = false;
    }
  }
  portEXIT_CRITICAL_ISR(&sonarMux);
}
bool proximityReady() {
  return TIMIAC_PROXIMITY_ENABLED && bothHealthy() && !operation.barrier.active() && !program.reserved() &&
    program.state != "ready" && (boards[0].homed & kHomedMask) == kHomedMask && (boards[1].homed & kHomedMask) == kHomedMask;
}
void configureProximity() {
#if TIMIAC_PROXIMITY_ENABLED
  static_assert(TIMIAC_SONAR_TRIG != TIMIAC_SONAR_ECHO && TIMIAC_SONAR_TRIG != TIMIAC_RTC_SDA &&
                TIMIAC_SONAR_TRIG != TIMIAC_RTC_SCL && TIMIAC_SONAR_ECHO != TIMIAC_RTC_SDA &&
                TIMIAC_SONAR_ECHO != TIMIAC_RTC_SCL, "RTC and ultrasonic pins must be different");
  pinMode(TIMIAC_SONAR_TRIG, OUTPUT); digitalWrite(TIMIAC_SONAR_TRIG, LOW);
  pinMode(TIMIAC_SONAR_ECHO, INPUT_PULLDOWN);
  attachInterrupt(digitalPinToInterrupt(TIMIAC_SONAR_ECHO), sonarEchoEdge, CHANGE);
  sonarSent = micros();
#endif
}
void proximityTick() {
#if TIMIAC_PROXIMITY_ENABLED
  bool ready = proximityReady();
  if (!ready) proximityGate.reset();
  uint32_t now = micros();
  if (sonarWaiting) {
    bool done; uint32_t width;
    portENTER_CRITICAL(&sonarMux);
    done = sonarDone; width = sonarWidth;
    if (done || now - sonarSent >= 60000) { sonarCapture = false; sonarDone = false; }
    portEXIT_CRITICAL(&sonarMux);
    if (!done && now - sonarSent < 60000) return;
    sonarWaiting = false; sonarSampleAt = millis();
    sonarDistance = timiac::sonarMillimetres(done ? width : 0);
    if (proximityGate.sample(sonarDistance, ready)) {
      String id = bootId + "-gesture-" + String(++sequence);
      const char* error = startBuiltinPreset(timiac::Preset::Curious, 0, id, hashText("gesture:curious"));
      if (error) logMessage("Gesture ignored: " + String(error));
      else { ++sonarTriggers; logMessage("Hand detected below 50 cm: curious creature"); }
    }
  }
  if (now - sonarSent < 120000) return;
  // No pulseIn() wait: keep networking, Stop and the board heartbeats responsive.
  if (digitalRead(TIMIAC_SONAR_ECHO)) {
    sonarSent = now; sonarSampleAt = millis(); sonarDistance = -2;
    proximityGate.sample(-2, ready); return;
  }
  portENTER_CRITICAL(&sonarMux);
  sonarCapture = true; sonarRiseSeen = false; sonarDone = false; sonarWidth = 0;
  portEXIT_CRITICAL(&sonarMux);
  sonarSent = micros(); sonarWaiting = true;
  digitalWrite(TIMIAC_SONAR_TRIG, HIGH); delayMicroseconds(20); digitalWrite(TIMIAC_SONAR_TRIG, LOW);
#endif
}
void writeProximityStatus(JsonObject doc) {
  bool fresh = sonarSampleAt && millis() - sonarSampleAt < 1000;
  doc["enabled"] = bool(TIMIAC_PROXIMITY_ENABLED);
  doc["armed"] = proximityReady() && proximityGate.armed;
  doc["ready"] = proximityReady(); doc["thresholdCm"] = 50;
  if (fresh && sonarDistance >= 200) doc["distanceCm"] = sonarDistance / 10.0f;
  else doc["distanceCm"] = nullptr;
  doc["triggers"] = sonarTriggers;
}
