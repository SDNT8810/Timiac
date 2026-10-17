#pragma once
File octopusImage;
unsigned octopusPhase = 0, octopusRetries = 0;
uint32_t octopusSent = 0, octopusOffset = 0, octopusNextOffset = 0;
String octopusCommand;

bool firmwareUpdateOwnsUart() { return octopusPhase && octopusPhase != 5; }
void octopusSend(const String& command) {
  octopusCommand = command; octopusSent = millis(); octopusRetries = 0;
  Serial.print(command); Serial.print('\n');
}
void octopusAbort() {
  if (octopusPhase && octopusPhase != 5) { Serial.print("M997 P4\n"); }
  octopusPhase = 0; octopusImage.close();
}
void octopusNext() {
  if (octopusOffset == firmwareUpdate.size) { octopusPhase = 3; octopusSend("M997 P2"); return; }
  uint8_t bytes[timiac::firmwareBlockBytes];
  const unsigned count = octopusImage.read(bytes, sizeof(bytes));
  if (!count || count > firmwareUpdate.size - octopusOffset) { octopusAbort(); otaFailed("Octopus cache read failed"); return; }
  octopusNextOffset = octopusOffset + count;
  const uint32_t crc = timiac::firmwareCrc(0xFFFFFFFFUL, bytes, count) ^ 0xFFFFFFFFUL;
  octopusPhase = 2; octopusSend("M997 P1 " + String(octopusOffset) + " " + String(crc) + " " + otaHex(bytes, count));
}
void octopusTick() {
  if (!octopusPhase && firmwareUpdating && firmwareUpdate.kind == "octopus" && firmwareUpdate.state == "waiting for UART") {
    if (firmwareUpdateCancel) { otaFailed("Update cancelled before SD transfer"); octopusImage.close(); return; }
    if (uart.session.state == timiac::OctopusSession::State::Ready && !uart.session.waiting) {
      firmwareUpdate.state = "writing SD";
      octopusPhase = 6; octopusOffset = octopusNextOffset = 0; otaSerialLine = "";
      octopusSend("M997 P4"); // Clear an abandoned staging session before opening another.
    } else if (uint32_t(millis() - octopusSent) > 20000) { octopusImage.close(); otaFailed("Octopus UART is not ready; reset a halted motor board first"); }
    return;
  }
  if (!octopusPhase) return;
  if (firmwareUpdateCancel && octopusPhase != 4 && octopusPhase != 5) { octopusAbort(); otaFailed("Update cancelled before activation"); return; }
  if (octopusPhase == 5) {
    if (uart.online() && uart.sdOtaSupported && uart.octopusSoftware == firmwareUpdate.revision) {
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
    if (line.startsWith("TIMIAC_FW_ERROR") || line.startsWith("Error:") || line.startsWith("Unknown command:")) {
      octopusAbort(); otaFailed(line); return;
    }
    if (octopusPhase == 6 && line == "TIMIAC_FW_ABORTED") {
      octopusPhase = 1; octopusSend("M997 P0 " + String(firmwareUpdate.size) + " " + String(firmwareUpdate.crc32));
    }
    if (line == "start" && octopusPhase == 4) {
      uart.receive(line); octopusPhase = 5; octopusSent = millis(); firmwareUpdate.state = "reconnecting"; return;
    }
    if ((octopusPhase == 1 || octopusPhase == 2) && line == "TIMIAC_FW_OFFSET " + String(octopusNextOffset)) {
      octopusOffset = octopusNextOffset; firmwareUpdate.done = octopusOffset; octopusNext();
    } else if (octopusPhase == 3 && line == "TIMIAC_FW_VERIFIED") {
      octopusPhase = 4; firmwareUpdate.state = "activating"; octopusSend("M997 P3");
    } else if (octopusPhase == 4 && line == "TIMIAC_FW_REBOOT") {
      uart.receive("start"); octopusPhase = 5; octopusSent = millis(); firmwareUpdate.state = "reconnecting"; return;
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
  if (octopusPhase && octopusPhase != 5 && uint32_t(millis() - octopusSent) > 3000) {
    if (++octopusRetries > 4) { const String phase = String(octopusPhase); octopusAbort(); otaFailed("Octopus SD acknowledgement timed out (phase " + phase + ")"); return; }
    octopusSent = millis(); Serial.print(octopusCommand); Serial.print('\n');
  }
}
