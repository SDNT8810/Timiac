// Compile-time tests: run using the same ARM compiler as the firmware.
#include "../shared/spider_homing.h"

constexpr bool simulate_homing(const uint8_t order[6]) {
  unsigned pulses[6] = {};
  unsigned trigger_at[6] = {};
  for (unsigned i = 0; i < 6; ++i) trigger_at[order[i]] = (i + 1) * 5;
  uint8_t hits = 0;
  for (unsigned tick = 1; tick <= 30; ++tick) {
    for (unsigned axis = 0; axis < 6; ++axis) {
      if (!spider_homing::stopped(hits, axis)) ++pulses[axis];
      if (tick == trigger_at[axis]) {
        hits = spider_homing::latch(hits, axis);
        hits = spider_homing::latch(hits, axis); // Repeated / bouncing switch.
      }
      const unsigned expected = tick < trigger_at[axis] ? tick : trigger_at[axis];
      if (pulses[axis] != expected) return false;
    }
    // No early switch may finish the block and stop the other five motors.
    if (spider_homing::complete(hits) != (tick == 30)) return false;
  }
  return true;
}

constexpr bool permutations(uint8_t order[6], unsigned depth, uint8_t chosen) {
  if (depth == 6) return simulate_homing(order);
  for (uint8_t axis = 0; axis < 6; ++axis) {
    if (chosen & (1U << axis)) continue;
    order[depth] = axis;
    if (!permutations(order, depth + 1, chosen | (1U << axis))) return false;
  }
  return true;
}

constexpr bool all_trigger_orders() {
  uint8_t order[6] = {};
  return permutations(order, 0, 0);
}

constexpr bool missing_switch_never_completes() {
  for (unsigned mask = 0; mask < 63; ++mask)
    if (spider_homing::complete(mask)) return false;
  return true;
}

static_assert(all_trigger_orders(), "Every switch order must stop only its own motor until all six stop.");
static_assert(missing_switch_never_completes(), "A missing switch must never count as successful homing.");
static_assert(!spider_homing::stopped(0, 0) && !spider_homing::stopped(0, 5), "Clearing the latch must restore normal moves.");
