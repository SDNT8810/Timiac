#pragma once
bool writePresetStep(const timiac::Step& step, JsonVariant doc) {
  for (unsigned b = 0; b < 2; ++b) {
    JsonArray lines = doc.createNestedArray(b ? "b" : "a");
    for (unsigned i = 0; i < step.count[b]; ++i) lines.add(String(step.lines[b][i]));
    if (lines.size() != step.count[b]) return false;
  }
  return validStep(doc);
}
// Keep the single-frame scratch buffer out of callers that also build full
// trajectories. They need this storage only in the non-trajectory branch.
__attribute__((noinline)) bool writePresetFrame(timiac::Preset preset, unsigned index, unsigned targetMinute, JsonVariant doc) {
  timiac::Step step;
  return timiac::presetStep(preset, index, targetMinute, kTravelMm, kMaxFeed, step) && writePresetStep(step, doc);
}
bool writePresetTrajectory(timiac::Preset preset, unsigned targetMinute, JsonVariant doc, unsigned variant = 0) {
  float positions[12];
  for (unsigned b = 0; b < 2; ++b) for (unsigned a = 0; a < 6; ++a) {
    const float value = boards[b].positions[a];
    if (!boards[b].known[a] || !isfinite(value) || value < 0 || value > kTravelMm) return false;
    positions[b * 6 + a] = value;
  }
  JsonArray a = doc.createNestedArray("a"), b = doc.createNestedArray("b");
  char acceleration[24];
  snprintf(acceleration, sizeof(acceleration), "M204 T%.3f",
    timiac::travelAcceleration * (preset == timiac::Preset::Struggle ? .8f : 1.0f));
  if (!a.add(String(acceleration)) || !b.add(String(acceleration))) return false;
  for (unsigned i = 0; i < timiac::presetCount(preset, variant); ++i) {
    timiac::Step frame;
    if (!timiac::presetTrajectoryFrame(preset, i, targetMinute, kTravelMm, kMaxFeed, positions, frame, variant)) return false;
    for (unsigned board = 0; board < 2; ++board) for (unsigned n = 0; n < frame.count[board]; ++n)
      if (!(board ? b : a).add(String(frame.lines[board][n]))) return false;
  }
  snprintf(acceleration, sizeof(acceleration), "M204 T%.3f", timiac::travelAcceleration);
  if (!a.add(String(acceleration)) || !b.add(String(acceleration))) return false;
  return a.size() && b.size() && validLines(a, true) && validLines(b, true);
}
const char* startBuiltinPreset(timiac::Preset preset, unsigned targetMinute, const String& id, uint32_t hash) {
  if (operation.barrier.active() || program.reserved()) return "A command or program owns the machine";
  if (!bothHealthy()) return "Both Octopus boards must be healthy and synchronized";
  if ((boards[0].homed & kHomedMask) != kHomedMask || (boards[1].homed & kHomedMask) != kHomedMask)
    return "Home all moving axes first";
  const unsigned variant = preset == timiac::Preset::Struggle
    ? timiac::chooseStruggleVariant(platformRandom(), lastStruggleVariant) : 0;
  unsigned total = timiac::presetCount(preset, variant);
  if (!total) return "Unknown preset";
  const bool trajectory = preset == timiac::Preset::Hour || preset == timiac::Preset::Struggle;
  if (trajectory && (!boards[0].trajectorySupported || !boards[1].trajectorySupported))
    return "Update both ESP bridges for continuous Next hour and Struggle";
  DynamicJsonDocument check(trajectory ? 8192 : 3072);
  if (trajectory) {
    if (!writePresetTrajectory(preset, targetMinute, check.to<JsonVariant>(), variant)) return "Continuous animation requires valid homed positions";
    total = 1; // The complete animation is one prepared operation, not one per frame.
  } else for (unsigned i = 0; i < total; ++i) {
    check.clear();
    if (!writePresetFrame(preset, i, targetMinute, check.to<JsonVariant>()))
      return "Preset failed command validation";
  }
  recordProgramReceipt();
  program.file.close(); program = Program();
  program.builtin = true; program.preset = preset; program.targetMinute = targetMinute;
  program.struggleVariant = variant;
  program.actionHash = hash; program.id = id;
  program.name = preset == timiac::Preset::Hour ? "Next hour" : preset == timiac::Preset::Curious ? "Curious creature" : "Struggle";
  if (preset == timiac::Preset::Struggle) {
    lastStruggleVariant = variant;
    program.name = "Struggle " + String(variant + 1) + "/7: " + timiac::strugglePatterns[variant].name;
  }
  program.total = total; program.uploaded = total; program.state = "running";
  armTimeMinutes = -1;
  recordProgramReceipt(); logMessage("Preset running: " + program.name);
  return nullptr;
}
