#pragma once
#include "cloud_certificate.h"
#include "ota_download_transport.h"

OtaDownloadSession* otaDownloadSession = nullptr;
WiFiClient otaBridgeUpload;
OtaSha256 otaForwardHash;
String otaImageId, otaBridgeNonce, otaBridgeBoot, otaBridgeResponse;
String otaAbortReason, otaUploadHeaders;
uint8_t otaPendingBytes[1024];
size_t otaPendingSize = 0, otaPendingOffset = 0, otaHeaderOffset = 0;
bool otaBridgeAccepted = false;
uint32_t otaSince = 0, otaProgressAt = 0, otaPollAt = 0, otaRebootAt = 0;
unsigned otaBridgeNumber = 0;

bool otaIdentifier(const String& id) {
  if (id.length() != 36) return false;
  for (unsigned i = 0; i < id.length(); ++i) if (id[i] != '-' && timiac::firmwareHex(id[i]) < 0) return false;
  return true;
}
bool otaParkNextBoot() {
  Preferences settings; if (!settings.begin("timiac-ota", false)) return false;
  const bool saved = settings.putBool("park", true) == 1; settings.end(); return saved;
}
void otaRememberReboot() {
  Preferences settings; if (!settings.begin("timiac-ota", false)) return;
  settings.putString("job", firmwareUpdate.id); settings.putString("hash", firmwareUpdate.sha256);
  settings.putString("revision", firmwareUpdate.revision); settings.putUInt("size", firmwareUpdate.size);
  settings.putBool("pending", true); settings.end();
}
void otaCloseDownload() {
  delete otaDownloadSession; otaDownloadSession = nullptr;
  otaBridgeUpload.stop(); otaUploadHeaders = ""; otaPendingSize = otaPendingOffset = otaHeaderOffset = 0;
}
void otaMasterFailed(const String& error) {
  // Never free a TLS object while its task still owns it. Keep the update
  // exclusive until that bounded connection attempt returns.
  if (otaOpenInFlight) { otaAbortReason = error; firmwareUpdate.state = "cancelling"; return; }
  otaCloseDownload(); otaFailed(error);
}
String otaBridgeUrl() { return "http://192.168.4." + String(240 + otaBridgeNumber) + ":3232/ota/info"; }
bool otaReadBridge(JsonDocument& doc) {
  WiFiClient socket; HTTPClient request; request.setConnectTimeout(1000); request.setTimeout(1000);
  if (!request.begin(socket, otaBridgeUrl())) return false;
  const int code = request.GET();
  const bool good = code == 200 && request.getSize() <= 2048 && !deserializeJson(doc, request.getStream());
  request.end(); return good;
}
__attribute__((noinline)) void acceptFirmwareUpdate(JsonVariant update) {
  if (!update.is<JsonObject>() || !update["id"].is<const char*>()) return;
  if (update["session"] != bootId) return;
  const String id = update["id"].as<String>();
  if (id == firmwareUpdate.id) return;
  const String target = update["target"] | "", kind = update["kind"] | "";
  const String imageId = update["imageId"] | "", hash = update["sha256"] | "", revision = update["revision"] | "";
  const uint32_t size = update["size"] | 0U;
  const bool master = target == "master" && (kind == "app" || kind == "filesystem");
  const bool bridge = (target == "esp1" || target == "esp2") && kind == "app";
  const bool octopus = (target == "octopus1" || target == "octopus2") && kind == "octopus";
  if (!otaIdentifier(id) || !otaIdentifier(imageId) || (!master && !bridge && !octopus) || !otaHashValid(hash) ||
      !size || size > 0x160000 || revision.isEmpty() || revision.length() > 32) return;
  if (firmwareUpdating || operation.barrier.active() || program.reserved() || boards[0].busy || boards[1].busy ||
      (lease.live() && !lease.owner.startsWith("cloud:"))) return;
  firmwareUpdate = FirmwareUpdateStatus(); firmwareUpdate.id = id; firmwareUpdate.target = target;
  firmwareUpdate.kind = kind; firmwareUpdate.size = size; firmwareUpdate.sha256 = hash;
  firmwareUpdate.revision = revision; firmwareUpdate.crc32 = update["crc32"] | 0U;
  firmwareUpdate.state = "stopping"; otaImageId = imageId;
  firmwareUpdate.phase = "Waiting for both bridges to acknowledge Stop";
  otaAbortReason = "";
  otaSince = otaProgressAt = millis();
  if (!otaParkNextBoot()) { otaFailed("Cannot save the parked state for an update reboot"); return; }
  otaBridgeNumber = master ? 0 : target.endsWith("1") ? 1 : 2;
  stopAll("Pausing the clock for a firmware update");
  firmwareUpdating = true; firmwareUpdateCancel = false;
}
bool otaStartDownload() {
  if (otaBridgeNumber) {
    if (!boardPeer(otaBridgeNumber - 1) || boards[otaBridgeNumber - 1].stopAck != stopToken) {
      otaMasterFailed("Bridge must reconnect and acknowledge Stop before updating"); return false;
    }
    DynamicJsonDocument info(2048);
    if (!otaReadBridge(info) || info["role"].as<String>() != "esp" + String(otaBridgeNumber) || info["busy"] == true ||
        !info["nonce"].is<const char*>() || (firmwareUpdate.kind == "octopus" && info["sdOtaSupported"] != true)) {
      otaMasterFailed("Bridge unavailable, busy, or initial SD update missing"); return false;
    }
    otaBridgeNonce = info["nonce"].as<String>();
    otaBridgeBoot = info["boot"].as<String>(); otaBridgeAccepted = false;
  }
  if (!otaOpenRequests) { otaMasterFailed("Firmware download worker unavailable"); return false; }
  auto* session = new (std::nothrow) OtaDownloadSession;
  if (!session) { otaMasterFailed("Not enough memory for firmware download"); return false; }
  session->url = String(TIMIAC_CLOUD_URL) + "/device/v1/devices/" + TIMIAC_CLOUD_DEVICE_ID + "/firmware/" + otaImageId;
  session->expectedSize = firmwareUpdate.size;
  if (xQueueSend(otaOpenRequests, &session, 0) != pdTRUE) { delete session; otaMasterFailed("Firmware connection worker busy"); return false; }
  otaOpenInFlight = true; firmwareUpdate.state = "connecting";
  firmwareUpdate.phase = "Connecting securely to the firmware gateway";
  return true;
}
bool otaBeginImageTransfer() {
  if (!otaBridgeNumber) {
    if (!otaWriter.begin(firmwareUpdate.kind, firmwareUpdate.size, firmwareUpdate.sha256)) { otaMasterFailed(otaWriter.error); return false; }
  } else {
    if (!otaBridgeUpload.connect(IPAddress(192,168,4,240 + otaBridgeNumber), 3232, 1000)) { otaMasterFailed("Cannot open bridge updater"); return false; }
    const String mac = otaAuthorization(otaBridgeNonce, firmwareUpdate.id, firmwareUpdate.target, firmwareUpdate.kind,
      firmwareUpdate.size, firmwareUpdate.sha256, firmwareUpdate.crc32, firmwareUpdate.revision);
    otaUploadHeaders = "POST /ota/image HTTP/1.1\r\nHost: timiac-bridge\r\nConnection: close\r\nContent-Type: application/octet-stream\r\nContent-Length: " + String(firmwareUpdate.size) +
      "\r\nX-Timiac-Target: " + firmwareUpdate.target + "\r\nX-Timiac-Kind: " + firmwareUpdate.kind +
      "\r\nX-Timiac-Job: " + firmwareUpdate.id + "\r\nX-Timiac-SHA256: " + firmwareUpdate.sha256 +
      "\r\nX-Timiac-CRC32: " + String(firmwareUpdate.crc32) + "\r\nX-Timiac-Revision: " + firmwareUpdate.revision +
      "\r\nX-Timiac-Auth: " + mac + "\r\n\r\n";
    otaHeaderOffset = 0;
    otaForwardHash.begin(); otaBridgeResponse = "";
  }
  firmwareUpdate.state = "downloading"; firmwareUpdate.phase = "Transferring firmware";
  otaPendingSize = otaPendingOffset = 0; otaProgressAt = millis(); return true;
}
void configureFirmwareUpdate() {
  configureOtaDownloadTransport();
  Preferences settings;
  if (!settings.begin("timiac-ota", false)) return;
  if (settings.getBool("park", false)) {
    releaseStartupPending = releaseClockEnabled = false;
  }
  if (settings.getBool("pending", false)) {
    firmwareUpdate.id = settings.getString("job", ""); firmwareUpdate.target = "master"; firmwareUpdate.kind = "app";
    firmwareUpdate.sha256 = settings.getString("hash", ""); firmwareUpdate.revision = settings.getString("revision", "");
    firmwareUpdate.size = firmwareUpdate.done = settings.getUInt("size", 0);
    firmwareUpdate.state = firmwareUpdate.revision == kSoftwareRevision ? "complete" : "failed";
    if (firmwareUpdate.state == "failed") firmwareUpdate.error = "Master rebooted with an unexpected revision";
    settings.putBool("pending", false);
  }
  settings.end();
}
__attribute__((noinline)) void firmwareUpdateTick() {
  OtaDownloadSession* opened = nullptr;
  if (otaOpenResponses && xQueueReceive(otaOpenResponses, &opened, 0) == pdTRUE) {
    otaOpenInFlight = false; otaDownloadSession = opened;
    if (!otaAbortReason.isEmpty()) { otaMasterFailed(otaAbortReason); return; }
    if (!opened->error.isEmpty()) { const String error = opened->error; otaMasterFailed(error); return; }
    if (firmwareUpdateCancel || stopRequested) { otaMasterFailed("Firmware update cancelled"); return; }
    if (!otaBeginImageTransfer()) return;
  }
  if (otaRebootAt && int32_t(millis() - otaRebootAt) >= 0) { ESP.restart(); return; }
  if (!firmwareUpdating) return;
  if (firmwareUpdateCancel || stopRequested) { otaMasterFailed("Firmware update cancelled"); return; }
  if (firmwareUpdate.state == "stopping") {
    if (!stopPending) otaStartDownload();
    else if (uint32_t(millis() - otaSince) > 20000) otaMasterFailed("Could not confirm the boards have stopped");
    return;
  }
  if (firmwareUpdate.state == "downloading") {
    // Both headers and image bytes tolerate TCP backpressure and short writes.
    // Keep the remainder until the next loop; never discard or re-hash it twice.
    if (otaBridgeNumber && otaHeaderOffset < otaUploadHeaders.length()) {
      const int written = writePeerChunk(otaBridgeUpload, otaUploadHeaders.c_str() + otaHeaderOffset, otaUploadHeaders.length() - otaHeaderOffset);
      if (written < 0) { otaMasterFailed("Bridge header connection lost"); return; }
      otaHeaderOffset += written;
      if (written) otaProgressAt = millis();
      else if (uint32_t(millis() - otaProgressAt) > 30000) otaMasterFailed("Bridge header transfer timed out");
      return;
    }
    if (!otaPendingSize) {
      const size_t available = otaDownloadSession->http.getStreamPtr()->available();
      const size_t wanted = min(sizeof(otaPendingBytes), min(available, size_t(firmwareUpdate.size - firmwareUpdate.done)));
      if (!wanted) {
        if (!otaDownloadSession->http.connected() || uint32_t(millis() - otaProgressAt) > 30000) otaMasterFailed("Firmware download interrupted; no activation requested");
        return;
      }
      const int count = otaDownloadSession->http.getStreamPtr()->read(otaPendingBytes, wanted);
      if (count <= 0) return;
      otaPendingSize = count; otaPendingOffset = 0;
    }
    int written = otaPendingSize;
    if (otaBridgeNumber) written = writePeerChunk(otaBridgeUpload, reinterpret_cast<const char*>(otaPendingBytes + otaPendingOffset), otaPendingSize - otaPendingOffset);
    else if (!otaWriter.write(otaPendingBytes, otaPendingSize)) { otaMasterFailed(otaWriter.error); return; }
    if (written < 0) { otaMasterFailed("Firmware transfer connection lost"); return; }
    if (!written) {
      if (uint32_t(millis() - otaProgressAt) > 30000) otaMasterFailed("Bridge firmware transfer stalled");
      return;
    }
    if (otaBridgeNumber) otaForwardHash.write(otaPendingBytes + otaPendingOffset, written);
    otaPendingOffset += written; firmwareUpdate.done += written; otaProgressAt = millis();
    if (otaPendingOffset == otaPendingSize) otaPendingSize = otaPendingOffset = 0;
    if (firmwareUpdate.done != firmwareUpdate.size) return;
    delete otaDownloadSession; otaDownloadSession = nullptr;
    if (otaBridgeNumber) {
      if (!otaEqual(otaForwardHash.finish(), firmwareUpdate.sha256)) { otaMasterFailed("Firmware SHA-256 mismatch"); return; }
      firmwareUpdate.state = "waiting for bridge"; firmwareUpdate.phase = "Waiting for verified board restart"; otaSince = millis();
    } else {
      if (firmwareUpdate.kind == "app") otaParkNextBoot();
      firmwareUpdate.state = "verifying"; firmwareUpdate.phase = "Checking SHA-256 and activating the complete image";
      checkpointFirmwareUpdate(true);
      if (!otaWriter.finish()) { otaMasterFailed(otaWriter.error); return; }
      if (firmwareUpdate.kind == "app") {
        otaRememberReboot(); firmwareUpdate.state = "rebooting"; otaRebootAt = millis() + 1000;
      } else { firmwareUpdate.state = "complete"; firmwareUpdating = false; }
    }
    return;
  }
  if (firmwareUpdate.state == "waiting for bridge") {
    while (otaBridgeUpload.available() && otaBridgeResponse.length() < 1600) otaBridgeResponse += char(otaBridgeUpload.read());
    const int bodyAt = otaBridgeResponse.indexOf("\r\n\r\n");
    if (!otaBridgeAccepted && bodyAt >= 0) {
      StaticJsonDocument<256> reply;
      if (!deserializeJson(reply, otaBridgeResponse.substring(bodyAt + 4))) {
        if (reply["accepted"] != true) { otaMasterFailed(reply["error"] | "Bridge rejected update"); return; }
        otaBridgeAccepted = true;
      }
    }
    if (uint32_t(millis() - otaPollAt) < 1000) return;
    otaPollAt = millis(); DynamicJsonDocument info(2048);
    if (otaReadBridge(info)) {
      if (firmwareUpdate.kind == "app") {
        const bool receipt = info["id"].as<String>() == firmwareUpdate.id && info["sha256"].as<String>() == firmwareUpdate.sha256 && info["state"] == "complete";
        if ((otaBridgeAccepted || receipt) && info["boot"].as<String>() != otaBridgeBoot && info["software"].as<String>() == firmwareUpdate.revision && info["role"].as<String>() == "esp" + String(otaBridgeNumber)) {
          otaCloseDownload(); firmwareUpdate.state = "complete"; firmwareUpdating = false;
        }
      } else if (info["id"].as<String>() == firmwareUpdate.id) {
        firmwareUpdate.phase = info["state"] | "waiting for bridge";
        if (info["state"] == "failed") { otaMasterFailed(info["error"] | "Octopus update failed"); return; }
        if (otaBridgeAccepted && info["state"] == "complete") { otaCloseDownload(); firmwareUpdate.state = "complete"; firmwareUpdating = false; }
        firmwareUpdate.done = info["done"] | firmwareUpdate.done;
      }
    }
    if (firmwareUpdating && uint32_t(millis() - otaSince) > 300000) otaMasterFailed("Board did not confirm its firmware within five minutes");
  }
}
