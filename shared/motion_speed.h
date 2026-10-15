#pragma once
#include <stdint.h>

namespace timiac {
constexpr unsigned defaultSpeedPercent = 75;
constexpr unsigned minSpeedPercent = 20;
constexpr unsigned maxSpeedPercent = 100;
inline bool validSpeedPercent(unsigned percent) {
  return percent >= minSpeedPercent && percent <= maxSpeedPercent;
}
inline uint32_t scaledMotionTimeout(uint32_t timeout, unsigned percent) {
  return (timeout * 100U + percent - 1U) / percent;
}
}
