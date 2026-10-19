#pragma once
#include "ota_local_request.h"

void localFirmwareReply(AsyncWebServerRequest* request, unsigned code, const String& error = "") {
  DynamicJsonDocument doc(2048);
  if (!error.isEmpty()) doc["error"] = error;
  else {
    doc["localSupported"] = true; doc["session"] = bootId; doc["nonce"] = localFirmwareNonce;
    doc["machine"] = timiac::machineId; doc["software"] = kSoftwareRevision;
    writeFirmwareUpdateStatus(doc.createNestedObject("ota"));
  }
  String body; serializeJson(doc, body);
  auto* response = request->beginResponse(code, "application/json", body);
  response->addHeader("Cache-Control", "no-store"); request->send(response);
}
bool localFirmwareClient(const IPAddress& ip) {
  if (ip[0] == 192 && ip[1] == 168 && ip[2] == 4 && ip[3] > 1 && ip[3] < 240) return true;
  if (WiFi.status() != WL_CONNECTED) return false;
  const uint32_t mask = WiFi.subnetMask(), station = WiFi.localIP(), remote = ip;
  return mask && remote && (remote & mask) == (station & mask) && remote != station;
}
void configureLocalFirmwareHttp() {
  localFirmwareNonce = bootId + "-" + String(platformRandom(), HEX);
  http.on("/api/firmware/local", HTTP_GET, [](AsyncWebServerRequest* request) {
    RuntimeLock lock; localFirmwareReply(request, 200);
  });
  auto* handler = new AsyncCallbackJsonWebHandler("/api/firmware/local", [](AsyncWebServerRequest* request, JsonVariant& doc) {
    RuntimeLock lock;
    const IPAddress sender = request->client()->remoteIP();
    if (!localFirmwareClient(sender)) { localFirmwareReply(request, 403, "Local updates require the clock hotspot or the same router LAN"); return; }
    if (const char* error = validateLocalFirmwareRequest(doc)) { localFirmwareReply(request, 403, error); return; }
    if (firmwareUpdating || otaOpenInFlight || otaRebootAt) { localFirmwareReply(request, 409, "A firmware update is already active"); return; }
    if (networkChanging) { localFirmwareReply(request, 409, "Wait for the router connection attempt to finish before updating over the hotspot"); return; }
    acceptFirmwareUpdate(doc, true);
    if (!firmwareUpdating || firmwareUpdate.id != doc["id"].as<String>()) {
      localFirmwareReply(request, 409, firmwareUpdate.id == doc["id"].as<String>() && !firmwareUpdate.error.isEmpty() ? firmwareUpdate.error : "Stop movement and wait for the boards before updating"); return;
    }
    // Never accept an arbitrary URL: only pull from the authenticated sender.
    otaLocalImageUrl = "http://" + sender.toString() + ":" + String(doc["port"].as<unsigned>()) + "/firmware/" + doc["imageId"].as<String>();
    localFirmwareNonce = bootId + "-" + String(platformRandom(), HEX);
    persistDiagnostic("Local Wi-Fi update accepted: " + firmwareUpdate.target + " " + firmwareUpdate.kind);
    localFirmwareReply(request, 202);
  }, 2048);
  handler->setMethod(HTTP_POST); handler->setMaxContentLength(1536); http.addHandler(handler);
}
