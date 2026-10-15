#pragma once
#include <stdint.h>

namespace timiac {
constexpr uint32_t proximityClearMs = 500, proximityDetectMs = 200;
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
}
