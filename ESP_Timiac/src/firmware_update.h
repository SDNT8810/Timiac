#pragma once
#include "../../shared/firmware_transfer.h"
#include "ota_writer.h"

struct FirmwareUpdateStatus {
  String id, target, kind, state = "idle", phase, error, sha256, revision;
  uint32_t size = 0, done = 0, crc32 = 0, at = 0;
} firmwareUpdate;
String otaNonce;
OtaWriter otaWriter;

void writeFirmwareUpdateStatus(JsonObject doc) {
  doc["supported"] = true; doc["active"] = firmwareUpdating;
  doc["marker"] = kOtaImageMarker; doc["id"] = firmwareUpdate.id;
  doc["target"] = firmwareUpdate.target; doc["kind"] = firmwareUpdate.kind;
  doc["state"] = firmwareUpdate.state; doc["error"] = firmwareUpdate.error;
  doc["phase"] = firmwareUpdating ? firmwareUpdate.phase : "";
  doc["size"] = firmwareUpdate.size; doc["done"] = firmwareUpdate.done;
  doc["sha256"] = firmwareUpdate.sha256; doc["revision"] = kSoftwareRevision;
}
void otaFailed(const String& reason) {
  otaWriter.abort(); firmwareUpdate.state = "failed"; firmwareUpdate.error = reason;
  firmwareUpdating = false; firmwareUpdateCancel = false;
  releaseClockEnabled = releaseStartupPending = false;
  logMessage("Firmware update failed: " + reason);
}

#if TIMIAC_MASTER
#include "ota_master.h"
#else
#include "ota_bridge.h"
#endif
