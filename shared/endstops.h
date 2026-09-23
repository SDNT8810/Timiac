#pragma once
#include <stdint.h>
#include <string.h>

namespace timiac {
constexpr uint32_t kEndstopFreshMs = 2000;

// M119 reports the configured G-code names: x_min ... c_min.
// Publish only after its acknowledgement, never a mixture of two reports.
struct EndstopReport {
  uint8_t known = 0, triggered = 0;
  uint32_t updatedAt = 0;
  uint8_t pendingKnown = 0, pendingTriggered = 0;
  void clear() { known = triggered = pendingKnown = pendingTriggered = 0; }
  void begin() { pendingKnown = pendingTriggered = 0; }
  void receive(const char* line) {
    const char* names[] = {"x_min:", "y_min:", "z_min:", "a_min:", "b_min:", "c_min:"};
    for (unsigned axis = 0; axis < 6; ++axis) {
      if (strncmp(line, names[axis], 6)) continue;
      const char* value = line + 6;
      while (*value == ' ' || *value == '\t') ++value;
      const uint8_t bit = 1U << axis;
      if (!strcmp(value, "TRIGGERED")) { pendingKnown |= bit; pendingTriggered |= bit; }
      else if (!strcmp(value, "open")) { pendingKnown |= bit; pendingTriggered &= ~bit; }
      else pendingKnown &= ~bit;
      return;
    }
  }
  void finish(uint32_t now) { known = pendingKnown; triggered = pendingTriggered; updatedAt = now; }
  bool fresh(uint32_t now) const { return known && uint32_t(now - updatedAt) < kEndstopFreshMs; }
};
} // namespace timiac
