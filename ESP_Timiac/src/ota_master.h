#pragma once
#include "cloud_certificate.h"

HTTPClient otaDownload;
WiFiClientSecure otaDownloadTls;
WiFiClient otaBridgeUpload;
OtaSha256 otaForwardHash;
String otaImageId, otaBridgeNonce, otaBridgeBoot, otaBridgeResponse;
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
void otaCloseDownload() { otaDownload.end(); otaBridgeUpload.stop(); }
void otaMasterFailed(const String& error) { otaCloseDownload(); otaFailed(error); }
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
  otaDownloadTls.setCACert(strlen(TIMIAC_CLOUD_CA_CERT) ? TIMIAC_CLOUD_CA_CERT : kCloudRunRootCa);
  otaDownload.setConnectTimeout(2000); otaDownload.setTimeout(5000); otaDownload.useHTTP10(true);
  const String url = String(TIMIAC_CLOUD_URL) + "/device/v1/devices/" + TIMIAC_CLOUD_DEVICE_ID + "/firmware/" + otaImageId;
  if (!otaDownload.begin(otaDownloadTls, url)) { otaMasterFailed("Cannot open firmware download"); return false; }
  otaDownload.addHeader("X-Timiac-Device-Key", TIMIAC_CLOUD_DEVICE_KEY);
  otaDownload.addHeader("Accept-Encoding", "identity");
  if (otaDownload.GET() != 200 || otaDownload.getSize() != int(firmwareUpdate.size)) { otaMasterFailed("Firmware download rejected or length mismatch"); return false; }
  if (!otaBridgeNumber) {
    if (!otaWriter.begin(firmwareUpdate.kind, firmwareUpdate.size, firmwareUpdate.sha256)) { otaMasterFailed(otaWriter.error); return false; }
  } else {
    if (!otaBridgeUpload.connect(IPAddress(192,168,4,240 + otaBridgeNumber), 3232)) { otaMasterFailed("Cannot open bridge updater"); return false; }
    const String mac = otaAuthorization(otaBridgeNonce, firmwareUpdate.id, firmwareUpdate.target, firmwareUpdate.kind,
      firmwareUpdate.size, firmwareUpdate.sha256, firmwareUpdate.crc32, firmwareUpdate.revision);
    otaBridgeUpload.print("POST /ota/image HTTP/1.1\r\nHost: timiac-bridge\r\nConnection: close\r\nContent-Type: application/octet-stream\r\nContent-Length: " + String(firmwareUpdate.size) +
      "\r\nX-Timiac-Target: " + firmwareUpdate.target + "\r\nX-Timiac-Kind: " + firmwareUpdate.kind +
      "\r\nX-Timiac-Job: " + firmwareUpdate.id + "\r\nX-Timiac-SHA256: " + firmwareUpdate.sha256 +
      "\r\nX-Timiac-CRC32: " + String(firmwareUpdate.crc32) + "\r\nX-Timiac-Revision: " + firmwareUpdate.revision +
      "\r\nX-Timiac-Auth: " + mac + "\r\n\r\n");
    otaForwardHash.begin(); otaBridgeResponse = "";
  }
  firmwareUpdate.state = "downloading"; otaProgressAt = millis(); return true;
}
void configureFirmwareUpdate() {
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
  if (otaRebootAt && int32_t(millis() - otaRebootAt) >= 0) { ESP.restart(); return; }
  if (!firmwareUpdating) return;
  if (firmwareUpdateCancel || stopRequested) { otaMasterFailed("Firmware update cancelled"); return; }
  if (firmwareUpdate.state == "stopping") {
    if (!stopPending) otaStartDownload();
    else if (uint32_t(millis() - otaSince) > 20000) otaMasterFailed("Could not confirm the boards have stopped");
    return;
  }
  if (firmwareUpdate.state == "downloading") {
    uint8_t bytes[1024];
    const size_t available = otaDownload.getStreamPtr()->available();
    const size_t wanted = min(sizeof(bytes), min(available, size_t(firmwareUpdate.size - firmwareUpdate.done)));
    if (!wanted) {
      if (!otaDownload.connected() || uint32_t(millis() - otaProgressAt) > 30000) otaMasterFailed("Firmware download interrupted; no activation requested");
      return;
    }
    const int count = otaDownload.getStreamPtr()->read(bytes, wanted);
    if (count <= 0) return;
    const bool written = otaBridgeNumber ? otaBridgeUpload.write(bytes, count) == size_t(count) : otaWriter.write(bytes, count);
    if (!written) { otaMasterFailed("Firmware transfer write failed"); return; }
    if (otaBridgeNumber) otaForwardHash.write(bytes, count);
    firmwareUpdate.done += count; otaProgressAt = millis();
    if (firmwareUpdate.done != firmwareUpdate.size) return;
    otaDownload.end();
    if (otaBridgeNumber) {
      if (!otaEqual(otaForwardHash.finish(), firmwareUpdate.sha256)) { otaMasterFailed("Firmware SHA-256 mismatch"); return; }
      firmwareUpdate.state = "waiting for bridge"; otaSince = millis();
    } else {
      if (firmwareUpdate.kind == "app") otaParkNextBoot();
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
        if (otaBridgeAccepted && info["boot"].as<String>() != otaBridgeBoot && info["software"].as<String>() == firmwareUpdate.revision && info["role"].as<String>() == "esp" + String(otaBridgeNumber)) {
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
