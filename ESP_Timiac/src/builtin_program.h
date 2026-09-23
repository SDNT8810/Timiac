#pragma once
bool writePresetStep(const timiac::Step& step, JsonVariant doc) {
  for (unsigned b = 0; b < 2; ++b) {
    JsonArray lines = doc.createNestedArray(b ? "b" : "a");
    for (unsigned i = 0; i < step.count[b]; ++i) lines.add(String(step.lines[b][i]));
    if (lines.size() != step.count[b]) return false;
  }
  return validStep(doc);
}
const char* startBuiltinPreset(timiac::Preset preset, unsigned targetMinute, const String& id, uint32_t hash) {
  if (operation.barrier.active() || program.reserved()) return "A command or program owns the machine";
  if (!bothHealthy()) return "Both Octopus boards must be healthy and synchronized";
  if ((boards[0].homed & kHomedMask) != kHomedMask || (boards[1].homed & kHomedMask) != kHomedMask)
    return "Home all moving axes first";
  unsigned total = timiac::presetCount(preset);
  if (!total) return "Unknown preset";
  DynamicJsonDocument check(3072);
  for (unsigned i = 0; i < total; ++i) {
    timiac::Step step; check.clear();
    if (!timiac::presetStep(preset, i, targetMinute, kTravelMm, kMaxFeed, step) || !writePresetStep(step, check.to<JsonVariant>()))
      return "Preset failed command validation";
  }
  recordProgramReceipt();
  program.file.close(); program = Program();
  program.builtin = true; program.preset = preset; program.targetMinute = targetMinute;
  program.actionHash = hash; program.id = id;
  program.name = preset == timiac::Preset::Hour ? "Next hour" : preset == timiac::Preset::Curious ? "Curious creature" : "Struggle";
  program.total = total; program.uploaded = total; program.state = "running";
  armTimeMinutes = -1;
  recordProgramReceipt(); logMessage("Preset running: " + program.name);
  return nullptr;
}
