#pragma once
#include "gcode.h"

namespace timiac {
enum class Preset : unsigned char { Struggle, Hour, Curious };
// Physical home is fully open. Simulator extension increases toward open.
// Keep native coordinates measured from the existing minimum endstop.
inline float actuatorForExtension(float percent, float travel) {
  return travel - percent * travel / 100;
}
inline bool validClockDate(unsigned year, unsigned month, unsigned day, unsigned hour, unsigned minute, unsigned second) {
  if (year < 2000 || year > 2099 || month < 1 || month > 12 || day < 1 || hour > 23 || minute > 59 || second > 59) return false;
  const unsigned days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  return day <= days[month - 1] + (month == 2 && year % 4 == 0 ? 1 : 0);
}
// Axis zero is 12 o'clock, proceeding clockwise across the two boards.
inline bool userAction(const char* action, unsigned hour, unsigned minute, float travel, float feed, Step& step) {
  step = Step();
  if (!strcmp(action, "home")) { step.both("G28 X Y Z A B C"); return true; }
  const bool open = !strcmp(action, "open"), close = !strcmp(action, "close"), clock = !strcmp(action, "show-time");
  if ((!open && !close && !clock) || (clock && (hour > 23 || minute > 59))) return false;
  for (unsigned b = 0; b < 2; ++b) {
    step.add(b, "G90");
    char move[128] = "G1"; unsigned length = 2;
    for (unsigned a = 0; a < 6; ++a) {
      unsigned index = b * 6 + a;
      float target = open ? 0 : travel;
      if (clock) target = index == minute / 5 ? 0 : index == hour % 12 ? travel / 2 : travel;
      length += snprintf(move + length, sizeof(move) - length, " %c%.3f", axes[a], target);
    }
    snprintf(move + length, sizeof(move) - length, " F%.3f", feed);
    step.add(b, move);
  }
  return true;
}
}
