#pragma once
#include <stdint.h>

namespace timiac {
inline bool expired(uint32_t now, uint32_t since, uint32_t interval) {
  return uint32_t(now - since) >= interval;
}

enum class Phase : uint8_t { Idle, Preparing, Running, Done, Failed };
struct Barrier {
  Phase phase = Phase::Idle;
  uint8_t prepared = 0, completed = 0;
  uint32_t since = 0;
  bool active() const { return phase == Phase::Preparing || phase == Phase::Running; }
  void begin(uint32_t now) { phase = Phase::Preparing; prepared = completed = 0; since = now; }
  bool canStart(bool bothHealthy) const { return phase == Phase::Preparing && prepared == 3 && bothHealthy; }
  void fail() { phase = Phase::Failed; }
  void done(uint8_t board) {
    if (phase != Phase::Running) return;
    completed |= board;
    if (completed == 3) phase = Phase::Done;
  }
  const char* name() const {
    switch (phase) {
      case Phase::Preparing: return "preparing";
      case Phase::Running: return "running";
      case Phase::Done: return "done";
      case Phase::Failed: return "failed";
      default: return "idle";
    }
  }
};
} // namespace timiac
