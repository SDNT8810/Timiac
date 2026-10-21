#define main coordinatorRegressionMain
#include "coordinator_test.cpp"
#undef main
#include "host/freertos/queue.h"
#include <atomic>
#include <sstream>
#include <chrono>
#include <mutex>
#include <condition_variable>
using std::min;
std::mutex gateMutex;
std::condition_variable gate;
bool opening = false, allowOpen = false;
unsigned connections = 0, tlsObjects = 0, activations = 0;
#include "../shared/firmware_transfer.h"
#include "../shared/firmware_image.h"
std::string image(2048, 'x');
String bridgeInfo;
struct WiFiClientSecure {
  WiFiClientSecure() { ++tlsObjects; } ~WiFiClientSecure() { --tlsObjects; }
  void setCACert(const char*) {} void setHandshakeTimeout(unsigned seconds) { assert(seconds == 20); }
  void stop() {}
  int lastError(char*, size_t) { return 0; }
};
struct DownloadStream {
  size_t offset = 0;
  size_t available() { return image.size() - offset; }
  int read(uint8_t* bytes, size_t n) { n = min(n, available()); memcpy(bytes, image.data() + offset, n); offset += n; return n; }
};
struct HTTPClient {
  bool firmware = true, local = false;
  DownloadStream body;
  std::istringstream info;
  void setConnectTimeout(unsigned) {} void setTimeout(unsigned) {}
  void useHTTP10(bool) {} void setReuse(bool) {}
  void addHeader(const char* name, const String&) { if (!strcmp(name, "X-Timiac-Device-Key")) assert(!local); }
  bool begin(WiFiClientSecure&, const String&) { firmware = true; return true; }
  bool begin(WiFiClient&, const String& url) { local = firmware = url.indexOf("/firmware/") >= 0; if (!firmware) info.str(bridgeInfo.c_str()); return true; }
  int GET() {
    if (!firmware) return 200;
    std::unique_lock<std::mutex> lock(gateMutex); ++connections; opening = true; gate.notify_all();
    gate.wait(lock, [] { return allowOpen; }); return 200;
  }
  int getSize() { return firmware ? int(image.size()) : int(bridgeInfo.length()); }
  std::istringstream& getStream() { return info; }
  DownloadStream* getStreamPtr() { return &body; }
  bool connected() { return body.available(); }
  void end() {}
};
struct OtaPreferences {
  bool begin(const char*, bool) { return true; } void end() {}
  size_t putBool(const char*, bool) { return 1; } bool getBool(const char*, bool fallback) { return fallback; }
  void putString(const char*, const String&) {} String getString(const char*, const char* fallback) { return fallback; }
  void putUInt(const char*, unsigned) {} unsigned getUInt(const char*, unsigned value) { return value; }
};
#define Preferences OtaPreferences
struct OtaSha256 {
  std::string bytes;
  void begin() { bytes.clear(); }
  void write(const uint8_t* data, size_t n) { bytes.append(reinterpret_cast<const char*>(data), n); }
  String finish() { assert(bytes == image); return std::string(64, 'a'); }
};
struct OtaWriter {
  String error;
  bool begin(const String&, unsigned, const String&) { return true; }
  bool write(uint8_t*, size_t) { return true; } bool finish() { ++activations; return true; } void abort() {}
} otaWriter;
struct FirmwareUpdateStatus {
  String id, target, kind, state = "idle", phase, error, sha256, revision;
  uint32_t size = 0, done = 0, crc32 = 0;
} firmwareUpdate;
void otaFailed(const String& reason) { firmwareUpdate.state = "failed"; firmwareUpdate.error = reason; firmwareUpdating = firmwareUpdateCancel = false; }
bool otaHashValid(const String& hash) { return hash.length() == 64; }
bool otaEqual(const String& a, const String& b) { return a == b; }
String otaAuthorization(const String&, const String&, const String&, const String&, unsigned, const String&, unsigned, const String&) { return "authenticated"; }
String otaMac(const String& message) { return "local-mac:" + message; }
struct { void restart() {} } ESP;
void checkpointFirmwareUpdate(bool = false) {}
#include "../ESP_Timiac/src/ota_master.h"
#include "../ESP_Timiac/src/ota_local_request.h"

void accept(const char* id, const char* target, bool local = false) {
  DynamicJsonDocument job(1024);
  job["id"] = id; job["session"] = bootId; job["target"] = target; job["kind"] = "app";
  job["imageId"] = "11111111-1111-4111-8111-111111111111";
  job["sha256"] = std::string(64, 'a'); job["size"] = image.size(); job["revision"] = kSoftwareRevision;
  acceptFirmwareUpdate(job.as<JsonVariant>(), local);
}
void setBridgeInfo(bool complete) {
  DynamicJsonDocument info(1024);
  info["role"] = "esp1"; info["nonce"] = "nonce"; info["busy"] = false;
  info["boot"] = complete ? "new-bridge-boot" : "old-bridge-boot"; info["software"] = kSoftwareRevision;
  if (complete) {
    info["id"] = firmwareUpdate.id; info["sha256"] = firmwareUpdate.sha256; info["state"] = "complete";
    info["target"] = firmwareUpdate.target; info["kind"] = firmwareUpdate.kind;
    info["size"] = info["done"] = firmwareUpdate.size; info["revision"] = firmwareUpdate.revision;
    info["machine"] = timiac::machineId; info["sdOtaSupported"] = true;
    info["octopusSoftware"] = firmwareUpdate.revision;
  }
  bridgeInfo = ""; serializeJson(info, bridgeInfo);
}
void waitOpening() {
  std::unique_lock<std::mutex> lock(gateMutex);
  assert(gate.wait_for(lock, std::chrono::seconds(3), [] { return opening; }));
}
void releaseOpen() { std::lock_guard<std::mutex> lock(gateMutex); allowOpen = true; gate.notify_all(); }
void collect() {
  for (unsigned n = 0; otaOpenInFlight && n < 100000; ++n) { firmwareUpdateTick(); std::this_thread::yield(); }
  assert(!otaOpenInFlight);
}
int main() {
  resetMaster(); connectBoard(0); connectBoard(1); configureFirmwareUpdate(); setBridgeInfo(false);
  assert(hostTaskCreations == 0); // No updater stack is reserved during normal clock operation.
  accept("22222222-2222-4222-8222-222222222222", "esp1"); assert(stopPending && firmwareUpdating);
  for (auto& board : boards) board.stopAck = stopToken;
  cloudInFlight = true;
  recoverStop(); firmwareUpdateTick();
  assert(!otaOpenInFlight && firmwareUpdate.state == "stopping" && !connections);
  cloudInFlight = false;
  networkLargestBlock = 16U * 1024; // Plenty of total RAM, no large TLS block.
  firmwareUpdateTick();
  assert(!otaOpenInFlight && firmwareUpdate.state == "stopping" && !connections);
  networkLargestBlock = 64U * 1024;
  recoverStop(); firmwareUpdateTick(); waitOpening();
  assert(firmwareUpdate.state == "connecting" && otaOpenInFlight);
  for (unsigned n = 0; n < 500; ++n) {
    ++hostNow;
    for (auto& p : peers) { p.rx = hostNow; p.socket.writeCapacity = 4096; }
    networkTick(); firmwareUpdateTick();
  }
  assert(!stopPending && firmwareUpdating && firmwareUpdate.state == "connecting");
  otaBridgeUpload.connectSucceeds = true; releaseOpen(); collect();
  assert(firmwareUpdate.state == "downloading");
  for (unsigned n = 0; firmwareUpdate.state == "downloading" && n < 10000; ++n) {
    ++hostNow; otaBridgeUpload.maxWrite = 17; otaBridgeUpload.writeCapacity = n % 3 ? 29 : 0;
    firmwareUpdateTick(); assert(firmwareUpdate.state != "failed");
  }
  assert(firmwareUpdate.state == "waiting for bridge" && firmwareUpdate.done == image.size());
  assert(otaForwardHash.bytes == image && tlsObjects == 0);
  const size_t body = otaBridgeUpload.wire.find("\r\n\r\n"); assert(body != std::string::npos);
  assert(otaBridgeUpload.wire.substr(body + 4) == image);
  hostNow += 1500; firmwareUpdateTick(); assert(firmwareUpdating);
  setBridgeInfo(true); hostNow += 1500; firmwareUpdateTick();
  assert(firmwareUpdate.state == "complete" && !firmwareUpdating && !otaBridgeAccepted);
  assert(timiac::cloudConnectionAllowed(false, false, false)); // Resume bridge progress reports.
  assert(!timiac::cloudConnectionAllowed(true, false, false));
  assert(!timiac::cloudConnectionAllowed(false, true, false));
  assert(!timiac::cloudConnectionAllowed(false, false, true));
  { std::lock_guard<std::mutex> lock(gateMutex); opening = allowOpen = false; }
  accept("33333333-3333-4333-8333-333333333333", "master");
  for (auto& board : boards) board.stopAck = stopToken;
  recoverStop(); firmwareUpdateTick(); waitOpening();
  firmwareUpdateCancel = true; firmwareUpdateTick();
  assert(firmwareUpdate.state == "cancelling" && firmwareUpdating && otaOpenInFlight);
  releaseOpen(); collect(); assert(firmwareUpdate.state == "failed" && !firmwareUpdating && !tlsObjects && !activations);
  // Local manifests are bound to the boot, one-time nonce, complete metadata,
  // image URL token and PC port. A stale/tampered request cannot reserve a flash.
  DynamicJsonDocument local(1536);
  localFirmwareNonce = "one-time-nonce";
  local["session"] = bootId; local["nonce"] = localFirmwareNonce;
  local["id"] = "44444444-4444-4444-8444-444444444444";
  local["imageId"] = "55555555-5555-4555-8555-555555555555";
  local["target"] = "master"; local["kind"] = "app"; local["size"] = 2048;
  local["sha256"] = std::string(64, 'a'); local["crc32"] = 0;
  local["revision"] = kSoftwareRevision; local["machine"] = timiac::machineId; local["port"] = 20000;
  local["auth"] = localFirmwareAuthorization(local.as<JsonVariant>());
  assert(!validateLocalFirmwareRequest(local.as<JsonVariant>()));
  local["port"] = 20001; assert(validateLocalFirmwareRequest(local.as<JsonVariant>())); local["port"] = 20000;
  local["target"] = "esp2"; assert(validateLocalFirmwareRequest(local.as<JsonVariant>())); local["target"] = "master";
  localFirmwareNonce = "next-nonce"; assert(validateLocalFirmwareRequest(local.as<JsonVariant>())); localFirmwareNonce = "one-time-nonce";
  local["machine"] = "TIMIAC2-000000000000"; assert(validateLocalFirmwareRequest(local.as<JsonVariant>())); local["machine"] = timiac::machineId;
  assert(!validateLocalFirmwareRequest(local.as<JsonVariant>()));

  // Local images use plain LAN HTTP only; cloud TLS and its key are not used.
  // Preserve partial-write/receipt behavior, and reject the wrong role BEFORE
  // sending the final block to a bridge that would otherwise auto-activate it.
  for (unsigned wrong = 0; wrong < 2; ++wrong) {
    // A local HTTP recovery remains possible below the cloud TLS budget.
    networkFreeHeap = 40U * 1024; networkLargestBlock = 16U * 1024;
    assert(!platformNetworkMemoryReady(true) && platformNetworkMemoryReady(false));
    image.assign(2048, '\0'); image[0] = char(0xE9);
    image.replace(120, 22, wrong ? "TIMIAC_OTA_ROLE:esp2" : "TIMIAC_OTA_ROLE:esp1");
    image.replace(250, strlen(timiac::machineId), timiac::machineId);
    image.replace(400, strlen(kSoftwareRevision), kSoftwareRevision);
    setBridgeInfo(false);
    accept(wrong ? "66666666-6666-4666-8666-666666666666" : "77777777-7777-4777-8777-777777777777", "esp1", true);
    otaLocalImageUrl = "http://192.168.4.2:20000/firmware/55555555-5555-4555-8555-555555555555";
    for (auto& board : boards) board.stopAck = stopToken;
    recoverStop(); firmwareUpdateTick(); collect();
    assert(otaDownloadSession && otaDownloadSession->localTransport);
    for (unsigned n = 0; firmwareUpdate.state == "downloading" && n < 10000; ++n) {
      ++hostNow; otaBridgeUpload.maxWrite = 17; otaBridgeUpload.writeCapacity = 4096; firmwareUpdateTick();
    }
    if (wrong) {
      assert(firmwareUpdate.state == "failed" && firmwareUpdate.error.indexOf("Local image role") >= 0);
      assert(firmwareUpdate.done < firmwareUpdate.size && !firmwareUpdating && !activations);
    } else {
      assert(firmwareUpdate.state == "waiting for bridge" && firmwareUpdate.done == image.size());
      setBridgeInfo(true); hostNow += 1500; firmwareUpdateTick();
      assert(firmwareUpdate.state == "complete" && !firmwareUpdating);
    }
  }
  // A durable exact SD receipt survives a bridge reboot and a lost HTTP ACK.
  // Reconnecting, an old running revision or another job is never sufficient.
  auto pendingOctopus = [] {
    firmwareUpdate = FirmwareUpdateStatus(); firmwareUpdate.id = "88888888-8888-4888-8888-888888888888";
    firmwareUpdate.kind = "octopus"; firmwareUpdate.target = "octopus1";
    firmwareUpdate.sha256 = std::string(64, 'a'); firmwareUpdate.revision = kSoftwareRevision;
    firmwareUpdate.size = firmwareUpdate.done = 2048; firmwareUpdate.state = "waiting for bridge";
    firmwareUpdating = true; otaBridgeNumber = 1; otaBridgeBoot = "old-bridge-boot";
    otaBridgeAccepted = false; otaSince = hostNow; otaPollAt = 0; setBridgeInfo(true);
  };
  pendingOctopus(); hostNow += 1500; firmwareUpdateTick();
  assert(firmwareUpdate.state == "complete" && !firmwareUpdating && !otaBridgeAccepted);
  for (const char* field : {"sha256", "target", "kind", "revision", "role", "machine", "size", "done"}) {
    pendingOctopus();
    DynamicJsonDocument info(1536); assert(!deserializeJson(info, bridgeInfo));
    if (!strcmp(field, "size") || !strcmp(field, "done")) info[field] = 1024;
    else info[field] = "wrong";
    bridgeInfo = ""; serializeJson(info, bridgeInfo); hostNow += 1500; firmwareUpdateTick();
    assert(firmwareUpdate.state == "failed" && !firmwareUpdating && firmwareUpdate.error.indexOf("does not match") >= 0);
  }
  pendingOctopus();
  DynamicJsonDocument info(1536); assert(!deserializeJson(info, bridgeInfo));
  info["octopusSoftware"] = "older-build"; bridgeInfo = ""; serializeJson(info, bridgeInfo);
  hostNow += 1500; firmwareUpdateTick(); assert(firmwareUpdating && firmwareUpdate.state == "waiting for bridge");
  setBridgeInfo(true); hostNow += 1500; firmwareUpdateTick(); assert(firmwareUpdate.state == "complete" && !firmwareUpdating);
  pendingOctopus(); assert(!deserializeJson(info, bridgeInfo));
  info["id"] = "previous-esp-job"; bridgeInfo = ""; serializeJson(info, bridgeInfo);
  hostNow += 1500; firmwareUpdateTick();
  assert(firmwareUpdate.state == "failed" && !firmwareUpdating && firmwareUpdate.error.indexOf("without this Octopus update receipt") >= 0);

  hostJoinOneShotTasks();
  assert(hostTaskCreations == connections && hostTaskCreations == 4);
  vQueueDelete(otaOpenResponses);
  std::cout << "Production master OTA: stalled TLS, heartbeat service, short TCP writes, exact reboot receipts and safe cancellation passed\n";
}
