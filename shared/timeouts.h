#pragma once
#include <stdint.h>
#include <string.h>

namespace timiac {
constexpr uint32_t commandTimeoutMs = 180000;
constexpr uint32_t jobTimeoutMs = 300000;
constexpr uint32_t homingCommandTimeoutMs = 180000;
constexpr uint32_t homingJobTimeoutMs = 180000;
inline bool homingCommand(const char* line) {
  return line && !strncmp(line, "G28", 3) && (line[3] == '\0' || line[3] == ' ');
}
inline uint32_t acknowledgementTimeout(const char* line) {
  return homingCommand(line) ? homingCommandTimeoutMs : commandTimeoutMs;
}
inline uint32_t operationTimeout(bool homing) {
  return homing ? homingJobTimeoutMs : jobTimeoutMs;
}
}
