#pragma once
File octopusImage;
unsigned octopusPhase = 0, octopusRetries = 0;
uint32_t octopusSent = 0, octopusOffset = 0, octopusNextOffset = 0;
String octopusCommand;
bool saveOctopusUpdateReceipt(bool complete);
// The installed STM32 Arduino UART has a 64-byte ring (63 usable bytes).
// Hex-encoding a 64-byte block produces a >150-character command. A single
// 16-byte block including its offset/CRC/newline fits even while Marlin's main
// loop is busy with SD or driver work. Older receivers already accept it.
constexpr unsigned octopusUartBlockBytes = 16;
static_assert(timiac::octopusFirmwareLimit < 1000000UL &&
  8 + 6 + 1 + 10 + 1 + 2 * octopusUartBlockBytes + 1 <= 63, "SD command must fit the installed UART RX ring");
const char* octopusPhaseName() {
  switch (octopusPhase) {
    case 1: return "opening SD"; case 2: return "writing SD block";
    case 3: return "verifying SD readback"; case 4: return "activating";
    case 5: return "reconnecting"; case 6: return "resynchronizing UART";
    default: return "idle";
  }
}

bool firmwareUpdateOwnsUart() { return octopusPhase && octopusPhase != 5; }
void octopusSend(const String& command) {
  octopusCommand = command; octopusSent = millis(); octopusRetries = 0;
  if (octopusPhase == 6) Serial.print('\n'); // Fence any truncated old line.
  Serial.print(command); Serial.print('\n');
}
void octopusAbort() {
  if (octopusPhase && octopusPhase != 5) { Serial.print("\nM997 P4\n"); }
  octopusPhase = 0; octopusImage.close();
}
void octopusNext() {
  if (octopusOffset == firmwareUpdate.size) {
    octopusPhase = 3; firmwareUpdate.phase = "Verifying complete SD image by readback"; octopusSend("M997 P2"); return;
  }
  uint8_t bytes[octopusUartBlockBytes];
  const unsigned count = octopusImage.read(bytes, sizeof(bytes));
  if (!count || count > firmwareUpdate.size - octopusOffset) { octopusAbort(); otaFailed("Octopus cache read failed"); return; }
  octopusNextOffset = octopusOffset + count;
  const uint32_t crc = timiac::firmwareCrc(0xFFFFFFFFUL, bytes, count) ^ 0xFFFFFFFFUL;
  octopusPhase = 2; octopusSend("M997 P1 " + String(octopusOffset) + " " + String(crc) + " " + otaHex(bytes, count));
}
void octopusTick() {
  if (!octopusPhase && firmwareUpdating && firmwareUpdate.kind == "octopus" && firmwareUpdate.state == "waiting for UART") {
    if (firmwareUpdateCancel) { otaFailed("Update cancelled before SD transfer"); octopusImage.close(); return; }
    if (localUart().session.state == timiac::OctopusSession::State::Ready && !localUart().session.waiting) {
      firmwareUpdate.state = "writing SD";
      firmwareUpdate.phase = "Writing SD via UART (16-byte blocks)";
      octopusPhase = 6; octopusOffset = octopusNextOffset = 0; otaSerialLine = "";
      localUart().lastReply = ""; localUart().lastReplyAt = 0;
      octopusSend("M997 P4"); // Clear an abandoned staging session before opening another.
    } else if (uint32_t(millis() - octopusSent) > 20000) { octopusImage.close(); otaFailed("Octopus UART is not ready; reset a halted motor board first"); }
    return;
  }
  if (!octopusPhase) return;
  if (firmwareUpdateCancel && octopusPhase != 4 && octopusPhase != 5) { octopusAbort(); otaFailed("Update cancelled before activation"); return; }
  if (octopusPhase == 5) {
    if (localUart().online() && localUart().sdOtaSupported && localUart().octopusSoftware == firmwareUpdate.revision) {
      if (!saveOctopusUpdateReceipt(true)) logMessage("Octopus verified; completed receipt write failed, pending receipt retained");
      firmwareUpdate.state = "complete"; firmwareUpdating = false; octopusPhase = 0;
      LittleFS.remove("/octopus.update");
    } else if (uint32_t(millis() - octopusSent) > 120000) { octopusPhase = 0; otaFailed("Octopus did not reconnect with the expected firmware; check its SD card"); }
    return;
  }
  unsigned budget = 2048;
  while (Serial.available() && budget--) {
    const char c = Serial.read();
    if (c != '\n') { if (c != '\r' && otaSerialLine.length() < 180) otaSerialLine += c; continue; }
    String line = otaSerialLine; otaSerialLine = ""; line.trim();
    if (line.startsWith("echo:")) { line.remove(0, 5); line.trim(); }
    if (!line.isEmpty()) { localUart().lastReply = line.substring(0, 100); localUart().lastReplyAt = millis(); }
    // A reset outside activation loses the staging session. Record it instead
    // of swallowing the banner and later claiming only an ACK timed out.
    if (line == "start" && octopusPhase != 4) {
      const String reason = "Octopus restarted while " + String(octopusPhaseName()) + " at byte " + String(octopusOffset);
      octopusPhase = 0; octopusImage.close(); localUart().receive(line); otaFailed(reason); return;
    }
    if (line.startsWith("TIMIAC_FW_ERROR") || line.startsWith("Error:") || line.startsWith("Unknown command:")) {
      // The leading newline may finish an abandoned partial command. Its
      // error is stale; only the following P4 ACK establishes a clean session.
      if (octopusPhase == 6) continue;
      octopusAbort(); otaFailed(line); return;
    }
    if (octopusPhase == 6 && line == "TIMIAC_FW_ABORTED") {
      octopusPhase = 1; octopusSend("M997 P0 " + String(firmwareUpdate.size) + " " + String(firmwareUpdate.crc32));
    }
    if (line == "start" && octopusPhase == 4) {
      localUart().receive(line); octopusPhase = 5; octopusSent = millis(); firmwareUpdate.state = "reconnecting"; return;
    }
    if ((octopusPhase == 1 || octopusPhase == 2) && line == "TIMIAC_FW_OFFSET " + String(octopusNextOffset)) {
      octopusOffset = octopusNextOffset; firmwareUpdate.done = octopusOffset; octopusNext();
    } else if (octopusPhase == 3 && line == "TIMIAC_FW_VERIFIED") {
      if (!saveOctopusUpdateReceipt(false)) {
        octopusAbort(); otaFailed("Cannot save Octopus activation receipt; no activation requested"); return;
      }
      octopusPhase = 4; firmwareUpdate.state = "activating";
      firmwareUpdate.phase = "Activating verified SD image"; octopusSend("M997 P3");
    } else if (octopusPhase == 4 && line == "TIMIAC_FW_REBOOT") {
      localUart().receive("start"); octopusPhase = 5; octopusSent = millis(); firmwareUpdate.state = "reconnecting"; return;
    }
  }
  if (octopusPhase == 4) {
    // Activation is sent once. A missing final UART ACK must not cause another
    // activation/abort or a false three-second failure while the bootloader runs.
    if (uint32_t(millis() - octopusSent) > 120000) {
      octopusPhase = 0; octopusImage.close(); otaFailed("Octopus activation was not confirmed; check its SD card");
    }
    return;
  }
  const uint32_t acknowledgementMs = octopusPhase == 3 ? 30000 : 10000;
  if (octopusPhase && octopusPhase != 5 && uint32_t(millis() - octopusSent) > acknowledgementMs) {
    if (++octopusRetries > 4) {
      const String error = "Octopus SD ACK timeout while " + String(octopusPhaseName()) + " at " + String(octopusOffset) + "/" + String(firmwareUpdate.size) +
        "; last reply: " + (localUart().lastReply.isEmpty() ? String("none") : localUart().lastReply);
      octopusAbort(); otaFailed(error); return;
    }
    octopusSent = millis();
    if (octopusPhase == 6) Serial.print('\n');
    Serial.print(octopusCommand); Serial.print('\n');
  }
}
