#pragma once
WiFiServer otaListener(3232);
WiFiClient otaConnection;
String otaHeaders, otaSerialLine;
bool otaReceiving = false;
uint32_t otaConnectionAt = 0, otaRestartAt = 0;
File octopusImage;
unsigned octopusPhase = 0, octopusRetries = 0;
uint32_t octopusSent = 0, octopusOffset = 0, octopusNextOffset = 0;
String octopusCommand;

void otaNewNonce() { otaNonce = bootId + "-" + String(platformRandom(), HEX); }
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
  if (firmwareUpdateCancel) { octopusAbort(); otaFailed("Update cancelled before activation"); return; }
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
    String line = otaSerialLine; otaSerialLine = "";
    if (line.startsWith("TIMIAC_FW_ERROR")) { octopusAbort(); otaFailed(line); return; }
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
  if (octopusPhase && octopusPhase != 5 && uint32_t(millis() - octopusSent) > 3000) {
    if (octopusPhase == 4 || ++octopusRetries > 4) { octopusAbort(); otaFailed("Octopus update acknowledgement timed out"); return; }
    octopusSent = millis(); Serial.print(octopusCommand); Serial.print('\n');
  }
}
void otaReply(unsigned code, const String& body) {
  otaConnection.print("HTTP/1.1 " + String(code) + (code == 200 ? " OK\r\n" : " Error\r\n"));
  otaConnection.print("Content-Type: application/json\r\nCache-Control: no-store\r\nConnection: close\r\nContent-Length: " + String(body.length()) + "\r\n\r\n");
  otaConnection.print(body); otaConnection.flush(); otaConnection.stop(); otaHeaders = "";
}
String otaHeader(const char* name) {
  const String key = "\r\n" + String(name) + ": "; const int start = otaHeaders.indexOf(key);
  if (start < 0) return "";
  const int end = otaHeaders.indexOf("\r\n", start + key.length());
  return otaHeaders.substring(start + key.length(), end);
}
void otaInfo() {
  DynamicJsonDocument doc(2048); writeFirmwareUpdateStatus(doc.to<JsonObject>());
  doc["nonce"] = otaNonce; doc["role"] = "esp" + String(TIMIAC_BOARD_ID);
  doc["boot"] = bootId;
  doc["software"] = kSoftwareRevision; doc["octopusSoftware"] = uart.octopusSoftware;
  doc["sdOtaSupported"] = uart.sdOtaSupported; doc["machine"] = timiac::machineId;
  doc["busy"] = firmwareUpdating || uart.job || operation.barrier.active();
  String result; serializeJson(doc, result); otaReply(200, result);
}
void otaAccept() {
  if (otaHeaders.startsWith("GET /ota/info HTTP/1.")) { otaInfo(); return; }
  if (!otaHeaders.startsWith("POST /ota/image HTTP/1.")) { otaReply(404, "{\"error\":\"Not found\"}"); return; }
  const String target = otaHeader("X-Timiac-Target"), kind = otaHeader("X-Timiac-Kind"), hash = otaHeader("X-Timiac-SHA256");
  const String id = otaHeader("X-Timiac-Job"), revision = otaHeader("X-Timiac-Revision");
  const uint32_t size = otaHeader("Content-Length").toInt(), crc = strtoul(otaHeader("X-Timiac-CRC32").c_str(), nullptr, 10);
  const bool own = target == "esp" + String(TIMIAC_BOARD_ID) && kind == "app";
  const bool octopus = target == "octopus" + String(TIMIAC_BOARD_ID) && kind == "octopus";
  if ((!own && !octopus) || !size || id.isEmpty() || id.length() > 64 || revision.length() > 32 ||
      !otaHashValid(hash) || !otaEqual(otaHeader("X-Timiac-Auth"), otaAuthorization(otaNonce, id, target, kind, size, hash, crc, revision))) {
    otaReply(403, "{\"error\":\"Invalid authenticated update\"}"); return;
  }
  if (firmwareUpdating || uart.job || operation.barrier.active() || (octopus && !uart.sdOtaSupported)) {
    otaReply(409, "{\"error\":\"Board busy or initial Octopus SD update missing\"}"); return;
  }
  firmwareUpdate = FirmwareUpdateStatus(); firmwareUpdate.id = id; firmwareUpdate.target = target;
  firmwareUpdate.kind = kind; firmwareUpdate.sha256 = hash; firmwareUpdate.revision = revision;
  firmwareUpdate.size = size; firmwareUpdate.crc32 = crc; firmwareUpdate.state = "receiving";
  firmwareUpdating = true; firmwareUpdateCancel = false; otaNewNonce();
  if (!otaWriter.begin(kind, size, hash)) { otaFailed(otaWriter.error); otaReply(400, "{\"error\":\"Cannot stage image\"}"); return; }
  otaReceiving = true; otaConnectionAt = millis(); otaHeaders = "";
}
void configureFirmwareUpdate() { LittleFS.begin(); otaNewNonce(); otaListener.begin(); }
__attribute__((noinline)) void firmwareUpdateTick() {
  octopusTick();
  if (otaRestartAt && int32_t(millis() - otaRestartAt) >= 0) { ESP.restart(); return; }
  if (!otaConnection.connected() && !otaConnection.available()) {
    if (otaReceiving) { otaReceiving = false; otaFailed("Upload connection lost; original firmware retained"); }
    if (otaListener.hasClient()) { otaConnection = otaListener.accept(); otaHeaders = ""; otaConnectionAt = millis(); }
    else return;
  }
  if (uint32_t(millis() - otaConnectionAt) > 30000 || (otaReceiving && firmwareUpdateCancel)) {
    if (otaReceiving) { otaReceiving = false; otaFailed("Upload cancelled or timed out"); }
    otaReply(408, "{\"error\":\"Upload cancelled or timed out\"}"); return;
  }
  if (!otaReceiving) {
    unsigned budget = 1536;
    while (otaConnection.available() && budget--) {
      otaHeaders += char(otaConnection.read()); otaConnectionAt = millis();
      if (otaHeaders.endsWith("\r\n\r\n")) { otaAccept(); break; }
      if (otaHeaders.length() > 1500) { otaReply(413, "{\"error\":\"Headers too large\"}"); return; }
    }
    return;
  }
  uint8_t bytes[1024];
  const size_t available = otaConnection.available(), remaining = otaWriter.size - otaWriter.received;
  const size_t wanted = min(sizeof(bytes), min(available, remaining));
  if (!wanted) return;
  const int count = otaConnection.read(bytes, wanted);
  if (count <= 0) return;
  otaConnectionAt = millis();
  if (!otaWriter.write(bytes, count)) { otaReceiving = false; otaFailed(otaWriter.error); otaReply(400, "{\"error\":\"Image write failed\"}"); return; }
  firmwareUpdate.done = otaWriter.received;
  if (otaWriter.received != otaWriter.size) return;
  otaReceiving = false;
  if (!otaWriter.finish()) { otaFailed(otaWriter.error); otaReply(400, "{\"error\":\"SHA-256 or activation failed\"}"); return; }
  if (firmwareUpdate.kind == "app") {
    firmwareUpdate.state = "rebooting"; otaReply(200, "{\"accepted\":true}"); otaRestartAt = millis() + 500;
  } else {
    octopusImage = LittleFS.open("/octopus.update", "r");
    if (!octopusImage) { otaFailed("Octopus cache unavailable"); otaReply(500, "{\"error\":\"Cache unavailable\"}"); return; }
    otaReply(200, "{\"accepted\":true}"); firmwareUpdate.state = "waiting for UART"; firmwareUpdate.done = 0;
    octopusPhase = 0; octopusSent = millis();
  }
}
