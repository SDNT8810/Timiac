#pragma once
#include <stdint.h>

namespace timiac {
inline bool expired(uint32_t now, uint32_t since, uint32_t interval) {
  return uint32_t(now - since) >= interval;
}

enum class Phase : uint8_t { Idle, Preparing, Running, Done, Failed };
inline bool validBoardMask(uint8_t mask) { return mask && !(mask & ~3U); }
inline uint8_t commandBoards(unsigned a, unsigned b) { return (a ? 1 : 0) | (b ? 2 : 0); }
inline bool coversBoards(uint8_t available, uint8_t required) {
  return validBoardMask(required) && (available & required) == required;
}
struct Barrier {
  Phase phase = Phase::Idle;
  uint8_t required = 3, prepared = 0, completed = 0;
  uint32_t since = 0;
  bool active() const { return phase == Phase::Preparing || phase == Phase::Running; }
  bool includes(unsigned board) const { return board < 2 && (required & (1U << board)); }
  void begin(uint32_t now, uint8_t boards = 3) {
    required = boards; phase = validBoardMask(boards) ? Phase::Preparing : Phase::Failed;
    prepared = completed = 0; since = now;
  }
  bool canStart(bool healthy) const { return phase == Phase::Preparing && coversBoards(prepared, required) && healthy; }
  void fail() { phase = Phase::Failed; }
  void done(uint8_t board) {
    if (phase != Phase::Running) return;
    completed |= board & required;
    if (coversBoards(completed, required)) phase = Phase::Done;
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
