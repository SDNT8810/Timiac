#pragma once
#include "cloud_certificate.h"

// Optional outbound relay for the Cloud Run dashboard. It is intentionally
// disabled until its URL and device credentials are supplied through
// config.local.h. Remote control never falls back to insecure TLS.
String cloudState = "disabled", cloudError, cloudLastAction;
uint32_t cloudLastSync = 0, cloudLastActionAt = 0;

bool cloudEnabled() {
  return strlen(TIMIAC_CLOUD_URL) && strlen(TIMIAC_CLOUD_DEVICE_ID) && strlen(TIMIAC_CLOUD_DEVICE_KEY);
}
void writeCloudStatus(JsonObject cloud) {
  cloud["enabled"] = cloudEnabled(); cloud["state"] = cloudState;
  cloud["error"] = cloudError; cloud["lastAction"] = cloudLastAction;
  cloud["lastSyncAgeMs"] = cloudLastSync ? millis() - cloudLastSync : 0;
}
void cloudSnapshot(JsonObject snapshot) {
  snapshot["session"] = bootId; snapshot["machineReady"] = bothHealthy();
  snapshot["fault"] = systemFault; snapshot["stopPending"] = stopPending || stopRequested;
  JsonObject clock = snapshot.createNestedObject("clock");
  writeRtcStatus(clock); writeNtpStatus(clock);
  clock["automatic"] = bool(TIMIAC_RELEASE); clock["running"] = releaseClockEnabled;
  clock["startupPending"] = releaseStartupPending;
  JsonObject operationStatus = snapshot.createNestedObject("operation");
  operationStatus["id"] = operation.id; operationStatus["state"] = operation.barrier.name();
  operationStatus["error"] = operation.error; operationStatus["homing"] = operation.homing;
  writeBoard(snapshot.createNestedObject("a"), 0); writeBoard(snapshot.createNestedObject("b"), 1);
}
const char* startCloudAction(const String& type, const String& requestId, const String& localTime) {
  if (type == "stop") { stopRequested = true; return nullptr; }
  if (operation.barrier.active() || program.reserved()) return "Clock is busy";
  if (lease.live() && !lease.owner.startsWith("cloud:")) return "A local dashboard has control";
  if (type == "set-time") {
    DateTime date;
    if (!parseRtcDate(localTime, date)) return "Remote time is invalid";
    if (!setRtcDate(date)) return rtcError.isEmpty() ? "Could not verify DS3231 write" : rtcError.c_str();
    armTimeMinutes = -1; return nullptr;
  }
  if (type != "home" && type != "open" && type != "close" && type != "show-time") return "Unknown remote action";
  if (!bothHealthy()) return "Both motor boards must be ready";
  if (type != "home" && ((boards[0].homed & kHomedMask) != kHomedMask || (boards[1].homed & kHomedMask) != kHomedMask))
    return "Home all arms before moving";
  unsigned targetMinute = 0;
  if (type == "show-time") {
    rtcTick(true);
    if (!rtcValid) return rtcError.c_str();
    targetMinute = rtcTime.hour() * 60 + rtcTime.minute();
  }
  timiac::Step step;
  timiac::userAction(type.c_str(), targetMinute / 60, targetMinute % 60, kTravelMm, kMaxFeed, step);
  DynamicJsonDocument command(2304);
  for (unsigned board = 0; board < 2; ++board) {
    JsonArray lines = command.createNestedArray(board ? "b" : "a");
    for (unsigned line = 0; line < step.count[board]; ++line) lines.add(step.lines[board][line]);
  }
  lease.owner = "cloud:" + requestId.substring(0, 24); lease.label = "Remote dashboard";
  lease.renewed = millis();
  startOperation(command["a"].as<JsonArray>(), command["b"].as<JsonArray>(),
                 "cloud-" + requestId.substring(0, 72), hashText("cloud:" + type + ":" + requestId));
  armTimeMinutes = !systemLatched && type == "show-time" ? int(targetMinute) : -1;
#if TIMIAC_RELEASE
  releaseClockEnabled = !systemLatched && (type == "home" || type == "show-time");
  releaseClockMinute = -1;
#endif
  return nullptr;
}
void receiveCloudAction(JsonVariant action) {
  if (!action.is<JsonObject>()) return;
  const String id = action["id"] | "";
  const String type = action["type"] | "";
  if (id.isEmpty() || id.length() > 96 || type.isEmpty()) return;
  cloudLastAction = type; cloudLastActionAt = millis();
  const char* error = startCloudAction(type, id, action["localTime"] | "");
  cloudError = error ? error : "";
  if (error) logMessage("Remote action refused: " + String(error));
  else logMessage("Remote action accepted: " + type);
}
void cloudTick() {
  if (!cloudEnabled()) return;
  if (WiFi.status() != WL_CONNECTED || wifiPending || networkChanging) {
    cloudState = "waiting for router"; return;
  }
  // Keep the bridge state machines responsive. Remote status updates are best
  // effort and never hold the main loop for more than 1.5 seconds.
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
  DynamicJsonDocument outbound(4096); cloudSnapshot(outbound.createNestedObject("status"));
  String payload; serializeJson(outbound, payload);
  const int code = request.POST(payload);
  if (code != 200) {
    cloudState = "connection failed";
    cloudError = "Cloud service returned HTTP " + String(code);
    request.end(); return;
  }
  DynamicJsonDocument inbound(768);
  const DeserializationError parsed = deserializeJson(inbound, request.getStream());
  request.end();
  if (parsed) { cloudState = "connection failed"; cloudError = "Invalid cloud response"; return; }
  cloudState = "connected"; cloudError = "";
  receiveCloudAction(inbound["action"]);
}
