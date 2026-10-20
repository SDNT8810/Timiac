// Compile the actual relay, with deterministic queue/memory pressure rather
// than a real router. Observe ownership at the worker handoff, not afterwards.
#include "host/Arduino.h"
#include <cstdlib>
#include <iostream>
#include <cmath>
#define ARDUINOJSON_ENABLE_ARDUINO_STRING 1
static void* liveSnapshotPool = nullptr;
static void* jsonAllocate(size_t size) {
  void* result = std::malloc(size);
  if (size == 12288) { assert(!liveSnapshotPool); liveSnapshotPool = result; }
  return result;
}
static void jsonFree(void* block) { if (block == liveSnapshotPool) liveSnapshotPool = nullptr; std::free(block); }
#define malloc jsonAllocate
#define free jsonFree
#include <ArduinoJson.h>
#undef free
#undef malloc
#define main coordinatorRegressionMain
#include "coordinator_test.cpp"
#undef main
#include <sstream>
#include <thread>
using std::min;
struct WiFiClientSecure {
  void setCACert(const char*) {} void setHandshakeTimeout(unsigned) {}
  int lastError(char*, size_t) { return 0; }
};
struct HTTPClient {
  std::istringstream stream{"{\"action\":null}"};
  void setConnectTimeout(unsigned) {} void setTimeout(unsigned) {}
  void useHTTP10(bool) {} void setReuse(bool) {}
  bool begin(WiFiClientSecure&, const String&) { return true; }
  void addHeader(const char*, const String&) {} void end() {}
  int POST(uint8_t*, size_t) { return 200; }
  static String errorToString(int) { return "mock failure"; }
  std::istringstream& getStream() { return stream; }
};
struct {
  uint32_t getFreeHeap() { return networkFreeHeap; }
  uint32_t getMaxAllocHeap() { return networkLargestBlock; }
} ESP;
unsigned uxTaskGetStackHighWaterMark(std::thread*) { return 6000; }
bool wifiPending = false, otaOpenInFlight = false;
uint32_t uplinkGeneration = 0;
void writeTlsMemoryStatus(JsonObject) {}
void* otaDownloadSession = nullptr;
struct { String state = "idle"; } firmwareUpdate;
struct DateTime {};
void rtcTick(bool) {}
bool parseRtcDate(const String&, DateTime&) { return true; }
bool setRtcDate(const DateTime&) { return true; }
String rtcError;
void writeRtcStatus(JsonObject clock) { clock["valid"] = true; }
void writeNtpStatus(JsonObject clock) { clock["internetSync"] = "synchronized"; }
void writeFirmwareUpdateStatus(JsonObject update) { update["state"] = firmwareUpdate.state; }
void acceptFirmwareUpdate(JsonVariant) {}
#include "../ESP_Timiac/src/cloud_relay.h"

unsigned handoffs = 0;
void observeHandoff(HostQueue* queue) {
  if (queue == cloudRequests) { assert(!liveSnapshotPool); ++handoffs; }
}
int main() {
  resetMaster(); connectBoard(0); connectBoard(1);
  // A brief real sensor arrival, refused before homing, must survive in the
  // cloud payload even after the hand has been withdrawn between polls.
  photoelectricLevel = 0; proximityTick(); hostNow += 220; proximityTick();
  assert(proximityLastOutcome == "ignored" && proximityLastReason == "Arms need homing");
  photoelectricLevel = 95; hostNow += 20; proximityTick();
  cloudRequests = xQueueCreate(1, sizeof(CloudExchange*));
  cloudResponses = xQueueCreate(1, sizeof(CloudExchange*));
  hostBeforeQueueSend = observeHandoff;
  networkFreeHeap = 200000; networkLargestBlock = 16000;
  cloudTick(); assert(!cloudInFlight && !handoffs); // Fragmented RAM.
  networkLargestBlock = 80000; networkFreeHeap = 80000;
  cloudTick(); assert(!cloudInFlight && !handoffs); // Insufficient total RAM.
  networkFreeHeap = 200000;
  otaOpenInFlight = true; cloudTick(); assert(!handoffs);
  otaOpenInFlight = false; otaDownloadSession = &firmwareUpdate;
  cloudTick(); assert(!handoffs);
  otaDownloadSession = nullptr; firmwareUpdating = true; firmwareUpdate.state = "stopping";
  cloudTick(); assert(!handoffs);
  // A native-board write/reboot must keep cloud progress alive after its TLS
  // image download is closed. Otherwise the gateway's job would expire.
  firmwareUpdate.state = "waiting for bridge"; cloudTick();
  assert(cloudInFlight && handoffs == 1 && !liveSnapshotPool);
  CloudExchange* exchange = nullptr;
  assert(xQueueReceive(cloudRequests, &exchange, 0) == pdTRUE);
  {
    DynamicJsonDocument payload(12288);
    assert(!deserializeJson(payload, exchange->payload));
    assert(payload["status"]["software"] == kSoftwareRevision && payload["status"]["ota"]["state"] == "waiting for bridge");
    const auto sensor = payload["status"]["proximity"];
    assert(sensor["pin"] == 34 && sensor["detected"] == false && sensor["millivolts"] == 95);
    assert(sensor["lastOutcome"] == "ignored" && sensor["lastReason"] == "Arms need homing");
    assert(sensor["lastEventTime"] == rtcText && sensor["ignored"] == 1 && !payload.overflowed());
  }
  exchange->response = "{\"action\":null}";
  assert(xQueueSend(cloudResponses, &exchange, 0) == pdTRUE);
  cloudTick(); assert(!cloudInFlight && cloudState == "connected" && handoffs == 1);
  // A recovered router bypasses the previous 60-second cloud failure backoff.
  WiFi.state = 6; cloudTick(); assert(cloudState == "waiting for router");
  cloudRetryMs = 60000; cloudLastSync = millis(); ++uplinkGeneration;
  WiFi.state = WL_CONNECTED; cloudTick(); assert(cloudInFlight && handoffs == 2);
  assert(xQueueReceive(cloudRequests, &exchange, 0) == pdTRUE);
  exchange->error = "TLS memory unavailable";
  assert(xQueueSend(cloudResponses, &exchange, 0) == pdTRUE);
  cloudTick(); assert(!cloudInFlight && cloudFailures == 1);
  hostBeforeQueueSend = nullptr; vQueueDelete(cloudRequests); vQueueDelete(cloudResponses);
  std::cout << "Cloud relay: fragmented/low RAM deferred, snapshot pool freed before TLS, firmware/cloud exclusion and board progress reporting passed\n";
}
