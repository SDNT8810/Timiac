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
  step.clear();
  const auto& frame = preset == Preset::Hour ? hourFrames[index] : preset == Preset::Curious ? curiousFrames[index] : struggleFrames[index];
  if (frame.dwell) {
    char line[24]; snprintf(line, sizeof(line), "G4 P%u", frame.dwell); step.both(line); return true;
  }
  float feed = frame.feed * travel / 100;
  if (feed > maxFeed) feed = maxFeed;
  for (unsigned b = 0; b < 2; ++b) {
    char line[128] = "G1"; unsigned length = 2;
    for (unsigned a = 0; a < 6; ++a) {
      float value = frame.positions[clockHourForArm(b * 6 + a)];
      if (value < 0) continue;
      length += snprintf(line + length, sizeof(line) - length, " %c%.3f", axes[a], actuatorForExtension(value, travel));
    }
    if (length == 2) continue;
    snprintf(line + length, sizeof(line) - length, " F%.3f", feed);
    step.add(b, "G90"); step.add(b, line);
  }
  return true;
}

// Give the two boards a shared nominal frame duration without inserting a
// full-machine completion barrier between frames. Only a stationary board
// dwells while the other moves. Marlin still owns acceleration and lookahead.
inline bool presetTrajectoryFrame(Preset preset, unsigned index, unsigned target, float travel, float maxFeed,
                                  float (&positions)[12], Step& step) {
  Step pose;
  if (!presetStep(preset, index, target, travel, maxFeed, pose)) return false;
  bool hasMove = false, hasDwell = false;
  for (unsigned b = 0; b < 2; ++b) for (unsigned n = 0; n < pose.count[b]; ++n) {
    hasMove |= !strncmp(pose.lines[b][n], "G1 ", 3);
    hasDwell |= !strncmp(pose.lines[b][n], "G4 ", 3);
  }
  if (hasDwell) {
    if (hasMove) return false;
    step = pose;
    return true;
  }
  if (!hasMove) return false;
  float next[12]; memcpy(next, positions, sizeof(next));
  float feed = maxFeed;
  for (unsigned b = 0; b < 2; ++b) for (unsigned n = 0; n < pose.count[b]; ++n) {
    const char* line = pose.lines[b][n];
    if (strncmp(line, "G1 ", 3)) continue;
    for (unsigned a = 0; a < 6; ++a)
      if (const char* word = strchr(line, axes[a])) next[b * 6 + a] = strtof(word + 1, nullptr);
    if (const char* word = strchr(line, 'F')) feed = strtof(word + 1, nullptr);
  }
  if (preset == Preset::Struggle) {
    // Enter by the nearer band edge, pulse 12% inward, then return to that
    // edge. Keep the old vector-feed convention at 80% so this is slower
    // even when several motors move together, and bounded from any pose.
    for (unsigned arm = 0; arm < 12; ++arm)
      next[arm] = index == 0 ? travel * (positions[arm] <= travel * .5f ? .3f : .7f)
                : index == 1 ? positions[arm] + travel * (positions[arm] <= travel * .5f ? .12f : -.12f)
                : travel * (positions[arm] <= travel * .5f ? .3f : .7f);
    feed = maxFeed * .8f;
  }
  float distance[2] = {}, span = 0;
  for (unsigned b = 0; b < 2; ++b) {
    float primary = 0, secondary = 0;
    for (unsigned a = 0; a < 6; ++a) {
      const float delta = next[b * 6 + a] - positions[b * 6 + a];
      (a < 3 ? primary : secondary) += delta * delta;
      if (fabsf(delta) > span) span = fabsf(delta);
    }
    // Marlin's linear XYZABC feed distance: XYZ if moving, otherwise ABC.
    distance[b] = sqrtf(primary > 0 ? primary : secondary);
    if (preset == Preset::Struggle && distance[b] > span) span = distance[b];
  }
  step.clear();
  if (span < .0001f) return true; // Already at this pose on both boards.
  const unsigned durationMs = unsigned(ceilf(span / feed * 60000));
  if (!durationMs || durationMs > 120000) return false;
  for (unsigned b = 0; b < 2; ++b) {
    char line[128];
    if (distance[b] < .0001f) snprintf(line, sizeof(line), "G4 P%u", durationMs);
    else {
      strcpy(line, "G1"); unsigned length = 2;
      for (unsigned a = 0; a < 6; ++a) if (fabsf(next[b * 6 + a] - positions[b * 6 + a]) >= .0001f)
        length += snprintf(line + length, sizeof(line) - length, " %c%.3f", axes[a], next[b * 6 + a]);
      snprintf(line + length, sizeof(line) - length, " F%.3f", distance[b] * 60000 / durationMs);
    }
    if (!validTrajectory(line, 6, travel, maxFeed)) return false;
    step.add(b, line);
  }
  memcpy(positions, next, sizeof(next));
  return true;
}
}
