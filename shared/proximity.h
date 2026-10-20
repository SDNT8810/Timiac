#pragma once
#include <stdint.h>

namespace timiac {
constexpr uint32_t proximityClearMs = 500, proximityDetectMs = 40;
struct ProximityGate {
  bool armed = false, initialized = false, detected = false;
  uint32_t changedAt = 0;
  void reset() { armed = initialized = detected = false; changedAt = 0; }
  bool sample(bool present, bool ready, uint32_t now) {
    // Consume gestures during work/faults, never queue them for later.
    if (!ready) { reset(); return false; }
    if (!initialized || present != detected) {
      detected = present; changedAt = now; initialized = true;
    }
    if (!present) {
      if (uint32_t(now - changedAt) >= proximityClearMs) armed = true;
    } else if (armed && uint32_t(now - changedAt) >= proximityDetectMs) {
      armed = false; return true;
    }
    return false;
  }
};
// Observe one debounced hand arrival even when motion is blocked. A held hand
// produces one event; only a continuous clear interval permits another.
struct ProximityObservation {
  bool initialized = false, present = false, reported = false;
  uint32_t changedAt = 0;
  void reset() { initialized = present = reported = false; changedAt = 0; }
  void samplingGap() { initialized = false; } // Retain the held-hand latch.
  bool sample(bool detected, uint32_t now) {
    if (!initialized || detected != present) { present = detected; changedAt = now; initialized = true; }
    if (!present && uint32_t(now - changedAt) >= proximityClearMs) reported = false;
    if (present && !reported && uint32_t(now - changedAt) >= proximityDetectMs) {
      reported = true; return true;
    }
    return false;
  }
};
}
