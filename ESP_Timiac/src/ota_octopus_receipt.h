#pragma once

// Written only after the complete SD image passed readback verification and
// BEFORE the one-shot activation command. A bridge reset must retain the exact
// job/hash; recovery only checks the running firmware and never replays P3.
bool saveOctopusUpdateReceipt(bool complete) {
  DynamicJsonDocument receipt(768);
  receipt["version"] = 1; receipt["id"] = firmwareUpdate.id;
  receipt["target"] = firmwareUpdate.target; receipt["hash"] = firmwareUpdate.sha256;
  receipt["revision"] = firmwareUpdate.revision; receipt["size"] = firmwareUpdate.size;
  receipt["crc32"] = firmwareUpdate.crc32; receipt["machine"] = timiac::machineId;
  receipt["boot"] = bootId; receipt["complete"] = complete;
  if (receipt.overflowed()) return false;
  auto file = LittleFS.open("/octopus-receipt.tmp", "w");
  if (!file) return false;
  const size_t expected = measureJson(receipt), written = serializeJson(receipt, file);
  file.flush(); file.close();
  return written == expected && LittleFS.rename("/octopus-receipt.tmp", "/octopus-receipt.json");
}

bool restoreOctopusUpdateReceipt() {
  auto file = LittleFS.open("/octopus-receipt.json", "r");
  if (!file) return false;
  DynamicJsonDocument receipt(768);
  const bool parsed = file.size() <= 1024 && !deserializeJson(receipt, file);
  file.close();
  const String id = receipt["id"] | "", revision = receipt["revision"] | "";
  if (!parsed || receipt["version"] != 1 || receipt["machine"] != timiac::machineId ||
      receipt["target"].as<String>() != "octopus" + String(TIMIAC_BOARD_ID) ||
      id.isEmpty() || id.length() > 64 || revision.isEmpty() || revision.length() > 32 ||
      !otaHashValid(receipt["hash"].as<String>()) || !receipt["size"].is<uint32_t>() ||
      !receipt["size"].as<uint32_t>() || receipt["size"].as<uint32_t>() > timiac::octopusFirmwareLimit ||
      !receipt["crc32"].is<uint32_t>() || !receipt["complete"].is<bool>() ||
      !receipt["boot"].is<const char*>() || receipt["boot"].as<String>().isEmpty() ||
      receipt["boot"].as<String>() == bootId) return false;
  firmwareUpdate.id = id; firmwareUpdate.target = receipt["target"].as<String>();
  firmwareUpdate.kind = "octopus"; firmwareUpdate.sha256 = receipt["hash"].as<String>();
  firmwareUpdate.revision = revision; firmwareUpdate.size = firmwareUpdate.done = receipt["size"].as<uint32_t>();
  firmwareUpdate.crc32 = receipt["crc32"].as<uint32_t>(); firmwareUpdate.error = "";
  const bool complete = receipt["complete"].as<bool>();
  firmwareUpdate.state = complete ? "complete" : "reconnecting";
  firmwareUpdate.phase = complete ? "" : "Confirming Octopus after bridge restart";
  firmwareUpdating = !complete; firmwareUpdateCancel = false;
  octopusPhase = complete ? 0 : 5; octopusSent = millis();
  return true;
}
