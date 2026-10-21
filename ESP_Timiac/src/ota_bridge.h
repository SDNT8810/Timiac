#pragma once
WiFiServer otaListener(3232);
WiFiClient otaConnection;
String otaHeaders, otaSerialLine;
bool otaReceiving = false;
uint32_t otaConnectionAt = 0, otaRestartAt = 0;
void otaNewNonce() { otaNonce = bootId + "-" + String(platformRandom(), HEX); }
#include "ota_octopus.h"
#include "ota_octopus_receipt.h"
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
  doc["software"] = kSoftwareRevision; doc["octopusSoftware"] = localUart().octopusSoftware;
  doc["sdOtaSupported"] = localUart().sdOtaSupported; doc["machine"] = timiac::machineId;
  doc["busy"] = firmwareUpdating || localUart().job || operation.barrier.active();
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
  if (firmwareUpdating || localUart().job || operation.barrier.active() || (octopus && !localUart().sdOtaSupported)) {
    otaReply(409, "{\"error\":\"Board busy or initial Octopus SD update missing\"}"); return;
  }
  // A newer authenticated job supersedes the previous Octopus receipt. In
  // particular, an ESP application reboot must restore its own new receipt.
  if (LittleFS.exists("/octopus-receipt.json") && !LittleFS.remove("/octopus-receipt.json")) {
    otaReply(507, "{\"error\":\"Cannot clear the previous Octopus update receipt\"}"); return;
  }
  firmwareUpdate = FirmwareUpdateStatus(); firmwareUpdate.id = id; firmwareUpdate.target = target;
  firmwareUpdate.kind = kind; firmwareUpdate.sha256 = hash; firmwareUpdate.revision = revision;
  firmwareUpdate.size = size; firmwareUpdate.crc32 = crc; firmwareUpdate.state = "receiving";
  firmwareUpdating = true; firmwareUpdateCancel = false; otaNewNonce();
  if (!otaWriter.begin(kind, size, hash)) { otaFailed(otaWriter.error); otaReply(400, "{\"error\":\"Cannot stage image\"}"); return; }
  otaReceiving = true; otaConnectionAt = millis(); otaHeaders = "";
}
bool saveBridgeUpdateReceipt() {
  DynamicJsonDocument receipt(768);
  receipt["id"] = firmwareUpdate.id; receipt["hash"] = firmwareUpdate.sha256;
  receipt["revision"] = firmwareUpdate.revision; receipt["size"] = firmwareUpdate.size;
  receipt["machine"] = timiac::machineId; receipt["boot"] = bootId;
  File file = LittleFS.open("/ota-receipt.tmp", "w");
  if (!file) return false;
  const size_t expected = measureJson(receipt), written = serializeJson(receipt, file);
  file.flush(); file.close();
  if (written != expected) return false;
  return LittleFS.rename("/ota-receipt.tmp", "/ota-receipt.json");
}
void configureFirmwareUpdate() {
  if (LittleFS.begin()) {
    File previous = LittleFS.open("/ota-last.json", "r");
    if (previous) { if (previous.size() <= 1024) lastUpdateCheckpoint = previous.readString(); previous.close(); }
    File file = LittleFS.open("/ota-receipt.json", "r");
    if (file) {
      DynamicJsonDocument receipt(768);
      if (!deserializeJson(receipt, file) && receipt["machine"] == timiac::machineId &&
          receipt["revision"] == kSoftwareRevision && receipt["boot"].as<String>() != bootId &&
          otaHashValid(receipt["hash"].as<String>())) {
        firmwareUpdate.id = receipt["id"].as<String>(); firmwareUpdate.sha256 = receipt["hash"].as<String>();
        firmwareUpdate.target = "esp" + String(TIMIAC_BOARD_ID); firmwareUpdate.kind = "app";
        firmwareUpdate.revision = kSoftwareRevision;
        firmwareUpdate.size = firmwareUpdate.done = receipt["size"] | 0U;
        firmwareUpdate.state = "complete";
      }
      file.close();
    }
    restoreOctopusUpdateReceipt();
  }
  otaNewNonce(); otaListener.begin();
}
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
  firmwareUpdate.state = "verifying"; checkpointFirmwareUpdate(true);
  if (!otaWriter.finish()) { otaFailed(otaWriter.error); otaReply(400, "{\"error\":\"SHA-256 or activation failed\"}"); return; }
  if (firmwareUpdate.kind == "app") {
    // Written only AFTER SHA verification and Update.end succeeded. The next
    // boot can prove this exact job completed even if its HTTP ACK was lost.
    if (!saveBridgeUpdateReceipt()) logMessage("OTA reboot receipt could not be saved; waiting for upload acknowledgement");
    firmwareUpdate.state = "rebooting"; otaReply(200, "{\"accepted\":true}"); otaRestartAt = millis() + 500;
  } else {
    octopusImage = LittleFS.open("/octopus.update", "r");
    if (!octopusImage) { otaFailed("Octopus cache unavailable"); otaReply(500, "{\"error\":\"Cache unavailable\"}"); return; }
    otaReply(200, "{\"accepted\":true}"); firmwareUpdate.state = "waiting for UART"; firmwareUpdate.done = 0;
    octopusPhase = 0; octopusSent = millis();
  }
}
