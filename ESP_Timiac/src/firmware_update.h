#pragma once
#include "../../shared/firmware_transfer.h"
#include "ota_writer.h"

struct FirmwareUpdateStatus {
  String id, target, kind, state = "idle", phase, error, sha256, revision;
  uint32_t size = 0, done = 0, crc32 = 0, at = 0;
} firmwareUpdate;
String otaNonce;
OtaWriter otaWriter;
String otaCheckpointState, otaCheckpointPhase, otaCheckpointId;
uint32_t otaCheckpointDone = 0, otaCheckpointAt = 0;
void checkpointFirmwareUpdate(bool force = false) {
  if (firmwareUpdate.id.isEmpty()) return;
  if (!force && otaCheckpointId == firmwareUpdate.id && otaCheckpointState == firmwareUpdate.state && otaCheckpointPhase == firmwareUpdate.phase &&
      (firmwareUpdate.done < otaCheckpointDone || firmwareUpdate.done - otaCheckpointDone < 65536) &&
      (firmwareUpdate.done == otaCheckpointDone || uint32_t(millis() - otaCheckpointAt) < 10000)) return;
  DynamicJsonDocument checkpoint(1024);
  checkpoint["id"] = firmwareUpdate.id; checkpoint["target"] = firmwareUpdate.target; checkpoint["kind"] = firmwareUpdate.kind;
  checkpoint["state"] = firmwareUpdate.state; checkpoint["phase"] = firmwareUpdate.phase; checkpoint["error"] = firmwareUpdate.error;
  checkpoint["done"] = firmwareUpdate.done; checkpoint["size"] = firmwareUpdate.size;
  checkpoint["sha256"] = firmwareUpdate.sha256; checkpoint["revision"] = firmwareUpdate.revision;
  checkpoint["boot"] = bootId; checkpoint["at"] = millis();
#if TIMIAC_MASTER && defined(ARDUINO_ARCH_ESP32)
  checkpoint["freeHeap"] = ESP.getFreeHeap(); checkpoint["minFreeHeap"] = ESP.getMinFreeHeap();
  checkpoint["utc"] = time(nullptr);
#endif
  String text; serializeJson(checkpoint, text); persistUpdateCheckpoint(text);
  persistDiagnostic("OTA " + firmwareUpdate.target + " " + firmwareUpdate.kind + " " + firmwareUpdate.state + " " + String(firmwareUpdate.done) + "/" + String(firmwareUpdate.size) + " " + firmwareUpdate.error);
  otaCheckpointId = firmwareUpdate.id; otaCheckpointState = firmwareUpdate.state; otaCheckpointPhase = firmwareUpdate.phase;
  otaCheckpointDone = firmwareUpdate.done; otaCheckpointAt = millis();
}

void writeFirmwareUpdateStatus(JsonObject doc) {
  doc["supported"] = true; doc["active"] = firmwareUpdating;
#if TIMIAC_MASTER
  doc["localSupported"] = true;
  doc["localMarker"] = kLocalOtaMarker;
#endif
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
  checkpointFirmwareUpdate(true);
  logMessage("Firmware update failed: " + reason);
}

#if TIMIAC_MASTER
#include "ota_master.h"
#else
#include "ota_bridge.h"
#endif
