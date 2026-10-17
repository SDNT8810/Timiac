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
  bool firmware = true;
  DownloadStream body;
  std::istringstream info;
  void setConnectTimeout(unsigned) {} void setTimeout(unsigned) {}
  void useHTTP10(bool) {} void setReuse(bool) {} void addHeader(const char*, const String&) {}
  bool begin(WiFiClientSecure&, const String&) { firmware = true; return true; }
  bool begin(WiFiClient&, const String&) { firmware = false; info.str(bridgeInfo.c_str()); return true; }
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
struct { void restart() {} } ESP;
void checkpointFirmwareUpdate(bool = false) {}
#include "../ESP_Timiac/src/ota_master.h"

void accept(const char* id, const char* target) {
  DynamicJsonDocument job(1024);
  job["id"] = id; job["session"] = bootId; job["target"] = target; job["kind"] = "app";
  job["imageId"] = "11111111-1111-4111-8111-111111111111";
  job["sha256"] = std::string(64, 'a'); job["size"] = image.size(); job["revision"] = kSoftwareRevision;
  acceptFirmwareUpdate(job.as<JsonVariant>());
}
void setBridgeInfo(bool complete) {
  DynamicJsonDocument info(1024);
  info["role"] = "esp1"; info["nonce"] = "nonce"; info["busy"] = false;
  info["boot"] = complete ? "new-bridge-boot" : "old-bridge-boot"; info["software"] = kSoftwareRevision;
  if (complete) { info["id"] = firmwareUpdate.id; info["sha256"] = firmwareUpdate.sha256; info["state"] = "complete"; }
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
  accept("22222222-2222-4222-8222-222222222222", "esp1"); assert(stopPending && firmwareUpdating);
  for (auto& board : boards) board.stopAck = stopToken;
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
  { std::lock_guard<std::mutex> lock(gateMutex); opening = allowOpen = false; }
  accept("33333333-3333-4333-8333-333333333333", "master");
  for (auto& board : boards) board.stopAck = stopToken;
  recoverStop(); firmwareUpdateTick(); waitOpening();
  firmwareUpdateCancel = true; firmwareUpdateTick();
  assert(firmwareUpdate.state == "cancelling" && firmwareUpdating && otaOpenInFlight);
  releaseOpen(); collect(); assert(firmwareUpdate.state == "failed" && !firmwareUpdating && !tlsObjects && !activations);
  { std::lock_guard<std::mutex> lock(otaOpenRequests->mutex); otaOpenRequests->closed = true; otaOpenRequests->ready.notify_all(); }
  otaOpenTask->join(); delete otaOpenTask; vQueueDelete(otaOpenRequests); vQueueDelete(otaOpenResponses);
  std::cout << "Production master OTA: stalled TLS, heartbeat service, short TCP writes, exact reboot receipts and safe cancellation passed\n";
}
