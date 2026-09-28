/**
 * Lumiac six-axis homing endstop latch.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once

#include <stdint.h>

namespace spider_homing {
  constexpr uint8_t all_axes = 0x3F; // X, Y, Z, I(A), J(B), K(C)

  constexpr uint8_t latch(const uint8_t stopped, const uint8_t axis) {
    return stopped | uint8_t(1U << axis);
  }

  constexpr bool stopped(const uint8_t mask, const uint8_t axis) {
    return (mask & (1U << axis)) != 0;
  }

  constexpr bool complete(const uint8_t mask) {
    return (mask & all_axes) == all_axes;
  }
}
