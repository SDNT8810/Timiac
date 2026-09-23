#pragma once
#include "clock.h"
#include "presets_generated.h"

namespace timiac {
inline unsigned nextClockMinute(unsigned base, bool nextFive) {
  return (nextFive ? (base / 5 + 1) * 5 : base + 60) % 1440;
}
inline unsigned presetCount(Preset preset) {
  switch (preset) {
    case Preset::Struggle: return sizeof(struggleFrames) / sizeof(struggleFrames[0]);
    case Preset::Hour: return sizeof(hourFrames) / sizeof(hourFrames[0]);
    case Preset::Curious: return sizeof(curiousFrames) / sizeof(curiousFrames[0]);
  }
  return 0;
}
inline bool presetStep(Preset preset, unsigned index, unsigned target, float travel, float maxFeed, Step& step) {
  if (index >= presetCount(preset) || target >= 1440) return false;
  if (preset == Preset::Hour && index + 1 == presetCount(preset))
    return userAction("show-time", target / 60, target % 60, travel, maxFeed, step);
  step = Step();
  const auto& frame = preset == Preset::Hour ? hourFrames[index] : preset == Preset::Curious ? curiousFrames[index] : struggleFrames[index];
  if (frame.dwell) {
    char line[24]; snprintf(line, sizeof(line), "G4 P%u", frame.dwell); step.both(line); return true;
  }
  float feed = frame.feed * travel / 100;
  if (feed > maxFeed) feed = maxFeed;
  for (unsigned b = 0; b < 2; ++b) {
    char line[128] = "G1"; unsigned length = 2;
    for (unsigned a = 0; a < 6; ++a) {
      float value = frame.positions[b * 6 + a];
      if (value < 0) continue;
      length += snprintf(line + length, sizeof(line) - length, " %c%.3f", axes[a], actuatorForExtension(value, travel));
    }
    if (length == 2) continue;
    snprintf(line + length, sizeof(line) - length, " F%.3f", feed);
    step.add(b, "G90"); step.add(b, line);
  }
  return true;
}
}
