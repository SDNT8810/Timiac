#pragma once
#include <stdint.h>

namespace timiac {
// AJ-SR04M mode 1: echo microseconds / 58 = centimetres.
// -1 is no echo, -2 is an invalid echo (including the sensor's blind zone).
inline int sonarMillimetres(uint32_t width) {
  if (!width) return -1;
  if (width < 1160 || width > 46400) return -2;
  return int(width * 10 / 58);
}
struct ProximityGate {
  bool armed = false;
  unsigned nearCount = 0, farCount = 0, missingCount = 0;
  void reset() { armed = false; nearCount = farCount = missingCount = 0; }
  bool sample(int millimetres, bool ready) {
    // Consume gestures during work/faults, never queue them for later.
    if (!ready) { reset(); return false; }
    if (millimetres == -1) {
      nearCount = farCount = 0;
      if (missingCount < 10) ++missingCount;
      if (missingCount >= 10) armed = true;
      return false;
    }
    missingCount = 0;
    if (millimetres >= 600 && millimetres <= 8000) {
      nearCount = 0;
      if (farCount < 3) ++farCount;
      if (farCount >= 3) armed = true;
      return false;
    }
    farCount = 0;
    if (!armed || millimetres < 200 || millimetres >= 500) { nearCount = 0; return false; }
    if (++nearCount < 3) return false;
    reset(); return true;
  }
};
}
