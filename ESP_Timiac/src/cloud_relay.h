#pragma once
#include "cloud_certificate.h"

// Outbound HTTPS relay. No router port needs to be opened.
String cloudState = "disabled", cloudError, cloudLastAction, cloudLastActionId, cloudActionError;
uint32_t cloudLastSync = 0, cloudLastActionAt = 0;

bool cloudEnabled() {
  return strlen(TIMIAC_CLOUD_URL) && strlen(TIMIAC_CLOUD_DEVICE_ID) && strlen(TIMIAC_CLOUD_DEVICE_KEY);
}
void writeCloudStatus(JsonObject cloud) {
  cloud["enabled"] = cloudEnabled(); cloud["state"] = cloudState;
  cloud["error"] = cloudError; cloud["lastAction"] = cloudLastAction;
  cloud["lastActionError"] = cloudActionError;
  cloud["lastSyncAgeMs"] = cloudLastSync ? millis() - cloudLastSync : 0;
}
void cloudSnapshot(JsonObject snapshot) {
  snapshot["session"] = bootId; snapshot["machineReady"] = bothHealthy();
  snapshot["fault"] = systemFault; snapshot["stopPending"] = stopPending || stopRequested;
  snapshot["speedPercent"] = motionSpeedPercent;
  JsonObject clock = snapshot.createNestedObject("clock");
  writeRtcStatus(clock); writeNtpStatus(clock);
  clock["automatic"] = bool(TIMIAC_RELEASE); clock["running"] = releaseClockEnabled;
  clock["startupPending"] = releaseStartupPending; clock["armMinute"] = armTimeMinutes;
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
void cloudTick() {
  if (!cloudEnabled()) return;
  if (WiFi.status() != WL_CONNECTED || wifiPending || networkChanging) {
    cloudState = "waiting for router"; return;
  }
  if (cloudLastSync && !timiac::expired(millis(), cloudLastSync, 5000)) return;
  cloudLastSync = millis();
  WiFiClientSecure client;
  client.setCACert(strlen(TIMIAC_CLOUD_CA_CERT) ? TIMIAC_CLOUD_CA_CERT : kCloudRunRootCa);
  HTTPClient request;
  const String endpoint = String(TIMIAC_CLOUD_URL) + "/device/v1/devices/" + TIMIAC_CLOUD_DEVICE_ID + "/sync";
  request.setConnectTimeout(1500); request.setTimeout(1500);
  if (!request.begin(client, endpoint)) { cloudState = "connection failed"; cloudError = "Could not start secure cloud connection"; return; }
  request.addHeader("Content-Type", "application/json");
  request.addHeader("X-Timiac-Device-Key", TIMIAC_CLOUD_DEVICE_KEY);
  DynamicJsonDocument outbound(6144); cloudSnapshot(outbound.createNestedObject("status"));
  if (outbound.overflowed()) {
    cloudState = "connection failed"; cloudError = "Cloud status is too large"; request.end(); return;
  }
  String payload; serializeJson(outbound, payload);
  const int code = request.POST(payload);
  if (code != 200) {
    cloudState = "connection failed"; cloudError = "Cloud service returned HTTP " + String(code); request.end(); return;
  }
  DynamicJsonDocument inbound(1024);
  const DeserializationError parsed = deserializeJson(inbound, request.getStream());
  request.end();
  if (parsed) { cloudState = "connection failed"; cloudError = "Invalid cloud response"; return; }
  cloudState = "connected"; cloudError = "";
  receiveCloudAction(inbound["action"]);
}
