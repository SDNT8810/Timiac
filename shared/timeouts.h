#pragma once
#include <stdint.h>
#include <string.h>
#include "motion_speed.h"

namespace timiac {
constexpr uint32_t commandTimeoutMs = 180000;
constexpr uint32_t jobTimeoutMs = 300000;
constexpr uint32_t homingCommandTimeoutMs = 180000;
constexpr uint32_t homingJobTimeoutMs = 300000;
inline bool homingCommand(const char* line) {
  return line && !strncmp(line, "G28", 3) && (line[3] == '\0' || line[3] == ' ');
}
inline uint32_t acknowledgementTimeout(const char* line, unsigned speedPercent = 100) {
  return scaledMotionTimeout(homingCommand(line) ? homingCommandTimeoutMs : commandTimeoutMs, speedPercent);
}
inline uint32_t operationTimeout(bool homing, unsigned speedPercent = 100) {
  return scaledMotionTimeout(homing ? homingJobTimeoutMs : jobTimeoutMs, speedPercent);
}
}
