#pragma once
#include "cloud_certificate.h"
#include "cloud_transport.h"

// Outbound HTTPS relay. No router port needs to be opened.
String cloudState = "disabled", cloudError, cloudLastAction, cloudLastActionId, cloudActionError;
uint32_t cloudLastSync = 0, cloudLastSuccess = 0, cloudLastActionAt = 0;
uint32_t cloudRetryMs = 5000, cloudFailures = 0, cloudLastDuration = 0;
uint32_t cloudStartFreeHeap = 0, cloudStartLargestBlock = 0;

bool cloudEnabled() {
  return strlen(TIMIAC_CLOUD_URL) && strlen(TIMIAC_CLOUD_DEVICE_ID) && strlen(TIMIAC_CLOUD_DEVICE_KEY);
}
void writeCloudStatus(JsonObject cloud) {
  cloud["enabled"] = cloudEnabled(); cloud["state"] = cloudState;
  cloud["error"] = cloudError; cloud["lastAction"] = cloudLastAction;
  cloud["lastActionError"] = cloudActionError;
  cloud["lastSyncAgeMs"] = cloudLastSuccess ? millis() - cloudLastSuccess : 0;
  cloud["inFlight"] = cloudInFlight; cloud["failures"] = cloudFailures; cloud["durationMs"] = cloudLastDuration;
  cloud["workerStackFree"] = cloudTask ? uxTaskGetStackHighWaterMark(cloudTask) : 0;
  cloud["startFreeHeap"] = cloudStartFreeHeap; cloud["startLargestFreeBlock"] = cloudStartLargestBlock;
}
void cloudSnapshot(JsonObject snapshot) {
  snapshot["session"] = bootId; snapshot["machineReady"] = bothHealthy();
  snapshot["fault"] = systemFault; snapshot["stopPending"] = stopPending || stopRequested;
  snapshot["speedPercent"] = motionSpeedPercent;
  snapshot["software"] = kSoftwareRevision;
  snapshot["firmware"] = timiac::machineId;
  writeReliabilityStatus(snapshot.createNestedObject("reliability"));
  JsonArray diagnostics = snapshot.createNestedArray("diagnostics"); writeDiagnosticBatch(diagnostics);
  const uint32_t firstLog = logSequence > 3 ? logSequence - 3 : 0;
  for (uint32_t i = firstLog; i < logSequence; ++i) {
    const auto& entry = logs[i % 32];
    JsonObject event = diagnostics.createNestedObject(); event["id"] = "r" + String(entry.id);
    event["boot"] = bootId; event["sequence"] = entry.id; event["at"] = entry.at; event["text"] = entry.text;
  }
  writeFirmwareUpdateStatus(snapshot.createNestedObject("ota"));
  JsonObject clock = snapshot.createNestedObject("clock");
  writeRtcStatus(clock); writeNtpStatus(clock);
  clock["automatic"] = bool(TIMIAC_RELEASE); clock["running"] = releaseClockEnabled;
  clock["startupPending"] = releaseStartupPending; clock["armMinute"] = armTimeMinutes;
  clock["pausedReason"] = releaseClockEnabled ? "" : clockPauseReason;
  JsonObject operationStatus = snapshot.createNestedObject("operation");
  operationStatus["id"] = operation.id; operationStatus["state"] = operation.barrier.name();
  operationStatus["error"] = operation.error; operationStatus["homing"] = operation.homing;
  JsonObject job = snapshot.createNestedObject("program");
  job["id"] = program.id; job["name"] = program.name; job["state"] = program.state; job["error"] = program.error;
  JsonObject control = snapshot.createNestedObject("control");
  control["owner"] = lease.live() ? lease.owner : "";
  JsonObject result = snapshot.createNestedObject("remoteAction");
  result["id"] = cloudLastActionId; result["type"] = cloudLastAction; result["error"] = cloudActionError;
  writeBoard(snapshot.createNestedObject("a"), 0); writeBoard(snapshot.createNestedObject("b"), 1);
}
const char* startCloudAction(const String& type, const String& requestId, JsonVariant action) {
  if (type == "stop") { stopRequested = true; return nullptr; }
  if (firmwareUpdating) return "Firmware update in progress";
  if (operation.barrier.active() || program.reserved()) return "Clock is busy";
  if (lease.live() && !lease.owner.startsWith("cloud:")) return "A local dashboard has control";
  if (type == "set-speed") return saveMotionSpeed(action);
  if (type == "set-time") {
    DateTime date;
    if (!action["localTime"].is<const char*>() || !parseRtcDate(action["localTime"].as<String>(), date))
      return "Remote time is invalid";
    if (!setRtcDate(date)) return rtcError.isEmpty() ? "Could not verify DS3231 write" : rtcError.c_str();
    armTimeMinutes = -1; return nullptr;
  }
  const bool nextTime = type == "next-five" || type == "next-hour";
  const bool animation = type == "next-hour" || type == "struggle";
  if (type != "home" && type != "open" && type != "close" && type != "show-time" && type != "next-five" && !animation)
    return "Unknown remote action";
  if (!bothHealthy()) return "Both motor boards must be ready";
  if (type != "home" && ((boards[0].homed & kHomedMask) != kHomedMask || (boards[1].homed & kHomedMask) != kHomedMask))
    return "Home all arms before moving";
  unsigned targetMinute = 0;
  if (type == "show-time" || type == "struggle" || nextTime) {
    rtcTick(true);
    if ((type == "show-time" || type == "struggle" || armTimeMinutes < 0) && !rtcValid) return rtcError.c_str();
    targetMinute = nextTime && armTimeMinutes >= 0 ? armTimeMinutes : rtcTime.hour() * 60 + rtcTime.minute();
    if (nextTime) targetMinute = timiac::nextClockMinute(targetMinute, type == "next-five");
  }
  const String operationId = "cloud-" + requestId.substring(0, 72);
  const uint32_t actionHash = hashText("cloud:" + type + ":" + requestId);
  if (animation) {
    const char* error = startBuiltinPreset(type == "next-hour" ? timiac::Preset::Hour : timiac::Preset::Struggle,
                                          targetMinute, operationId, actionHash);
    if (error) return error;
    lease.owner = "cloud:" + requestId.substring(0, 24); lease.label = "Remote dashboard"; lease.renewed = millis();
    trackManualClockAction(type.c_str());
    return nullptr;
  }
  timiac::Step step;
  timiac::userAction(nextTime ? "show-time" : type.c_str(), targetMinute / 60, targetMinute % 60, kTravelMm, kMaxFeed, step);
  DynamicJsonDocument command(2304);
  for (unsigned board = 0; board < 2; ++board) {
    JsonArray lines = command.createNestedArray(board ? "b" : "a");
    for (unsigned line = 0; line < step.count[board]; ++line) lines.add(step.lines[board][line]);
  }
  if (command.overflowed()) return "Remote action is too large";
  lease.owner = "cloud:" + requestId.substring(0, 24); lease.label = "Remote dashboard"; lease.renewed = millis();
  startOperation(command["a"].as<JsonArray>(), command["b"].as<JsonArray>(), operationId, actionHash);
  armTimeMinutes = !systemLatched && (type == "show-time" || nextTime) ? int(targetMinute) : -1;
  trackManualClockAction(type.c_str());
  return nullptr;
}
void receiveCloudAction(JsonVariant action) {
  if (!action.is<JsonObject>()) return;
  const String id = action["id"] | "", type = action["type"] | "";
  if (id.isEmpty() || id.length() > 96 || type.isEmpty() || id == cloudLastActionId) return;
  cloudLastActionId = id; cloudLastAction = type; cloudLastActionAt = millis();
  const char* error = action["session"] != bootId ? "Remote action belongs to an earlier controller session" :
    startCloudAction(type, id, action);
  cloudActionError = error ? error : "";
  if (error) logMessage("Remote action refused: " + String(error));
  else logMessage("Remote action accepted: " + type);
}
void configureCloud() {
  if (!cloudEnabled()) return;
  if (!configureCloudTransport()) { cloudState = "unavailable"; cloudError = "Cloud worker memory unavailable; local clock remains active"; }
}
void recordCloudFailure(const String& error) {
  if (cloudState != "connection failed" || cloudError != error) persistDiagnostic("Cloud connection failed: " + error);
  cloudState = "connection failed"; cloudError = error; ++cloudFailures;
  cloudRetryMs = min(uint32_t(60000), cloudRetryMs * 2);
}
void cloudTick() {
  if (!cloudEnabled() || !cloudRequests || !cloudResponses) return;
  CloudExchange* completed = nullptr;
  if (xQueueReceive(cloudResponses, &completed, 0) == pdTRUE) {
    cloudInFlight = false; cloudLastDuration = completed->elapsed;
    if (!completed->error.isEmpty()) {
      recordCloudFailure(completed->error);
    } else {
      DynamicJsonDocument inbound(2048);
      if (deserializeJson(inbound, completed->response)) {
        recordCloudFailure("Invalid cloud response");
      } else {
        if (cloudState != "connected") persistDiagnostic("Cloud connection established");
        cloudState = "connected"; cloudError = ""; cloudLastSuccess = millis(); cloudRetryMs = 5000;
        receiveCloudAction(inbound["action"]);
        if (!inbound["action"].is<JsonObject>()) acceptFirmwareUpdate(inbound["update"]);
      }
    }
    delete completed;
  }
  if (WiFi.status() != WL_CONNECTED || wifiPending || networkChanging) {
    cloudState = "waiting for router"; return;
  }
  if (cloudInFlight || (cloudLastSync && !timiac::expired(millis(), cloudLastSync, cloudRetryMs))) return;
  // One TLS session at a time. Keep reporting while an Octopus writes/reboots,
  // after its image download has released the TLS buffers.
  if (!timiac::cloudConnectionAllowed(otaOpenInFlight, otaDownloadSession != nullptr,
      firmwareUpdating && firmwareUpdate.state == "stopping")) return;
  if (!platformNetworkMemoryReady(true)) { cloudError = "Waiting for cloud connection memory"; return; }
  CloudExchange* exchange = new (std::nothrow) CloudExchange;
  if (!exchange) { cloudError = "Waiting for cloud connection memory"; return; }
  {
    DynamicJsonDocument outbound(12288); cloudSnapshot(outbound.createNestedObject("status"));
    if (outbound.overflowed()) { delete exchange; cloudState = "connection failed"; cloudError = "Cloud status is too large"; return; }
    if (!exchange->payload.reserve(measureJson(outbound)) || !serializeJson(outbound, exchange->payload)) {
      delete exchange; cloudError = "Waiting for cloud status memory"; return;
    }
  } // Free the 12 KB snapshot BEFORE the worker can start its handshake.
  cloudStartFreeHeap = ESP.getFreeHeap(); cloudStartLargestBlock = ESP.getMaxAllocHeap();
  if (!platformNetworkMemoryReady(true)) { delete exchange; cloudError = "Waiting for cloud connection memory"; return; }
  if (xQueueSend(cloudRequests, &exchange, 0) != pdTRUE) { delete exchange; return; }
  cloudInFlight = true; cloudLastSync = millis();
}
