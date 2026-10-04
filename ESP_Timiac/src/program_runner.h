#pragma once
void programTick() {
  if (program.state == "uploading" && millis() - program.activity > 60000) {
    program.state = "faulted"; program.error = "Incomplete upload expired; no movement started";
    program.file.close(); logMessage(program.error);
  }
  if (program.hasStep && operation.barrier.phase == timiac::Phase::Done) {
    program.hasStep = false; ++program.completed;
    if (program.completed == program.total) {
      program.state = "done"; program.file.close();
      if (program.builtin && program.preset == timiac::Preset::Hour) armTimeMinutes = program.targetMinute;
      recordProgramReceipt(); logMessage("Program complete");
    }
  }
  if (program.state != "running" || operation.barrier.active() || program.hasStep) return;
  if (!bothHealthy()) { stopAll("Board link lost between program steps"); return; }
  const bool trajectory = program.builtin && (program.preset == timiac::Preset::Hour || program.preset == timiac::Preset::Struggle);
  DynamicJsonDocument doc(trajectory ? 8192 : 3072);
  if (trajectory) {
    if (!writePresetTrajectory(program.preset, program.targetMinute, doc.to<JsonVariant>())) { stopAll("Continuous animation validation failed"); return; }
  } else if (program.builtin) {
    if (!writePresetFrame(program.preset, program.completed, program.targetMinute, doc.to<JsonVariant>())) {
      stopAll("Preset validation failed"); return;
    }
  } else {
    String line = program.file.readStringUntil('\n');
    if (deserializeJson(doc, line) || !validStep(doc.as<JsonVariant>())) { stopAll("Stored program validation failed"); return; }
  }
  if (!homingReady(doc.as<JsonVariant>())) {
    program.state = "faulted"; program.error = "Program requires homing before motion"; program.file.close(); armTimeMinutes = -1; recordProgramReceipt(); logMessage(program.error); return;
  }
  program.hasStep = true;
  startOperation(doc["a"].as<JsonArray>(), doc["b"].as<JsonArray>(), program.id + "-" + String(program.completed), commandHash(doc["a"], doc["b"]), 3, trajectory);
}
