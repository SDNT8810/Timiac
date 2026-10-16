// Compile the actual coordinator and actual cloud task with independent host
// threads. Hold HTTP indefinitely while a whole day of automatic motion runs.
#define TIMIAC_RELEASE 1
#define main coordinatorRegressionMain
#include "coordinator_test.cpp"
#undef main
#include <atomic>
#include <sstream>
#include <chrono>
#include <condition_variable>
#include <mutex>
std::mutex httpMutex;
std::condition_variable httpGate;
bool httpStarted = false, httpReleased = false;
std::atomic<unsigned> liveTls{0}, finishedHttp{0};
unsigned httpCode = 503;
const char kCloudRunRootCa[] = "test-public-certificate";
struct WiFiClientSecure {
  WiFiClientSecure() { ++liveTls; }
  ~WiFiClientSecure() { --liveTls; }
  void setCACert(const char* cert) { assert(cert && *cert); }
  void setHandshakeTimeout(unsigned seconds) { assert(seconds == 3); }
};
struct HTTPClient {
  std::istringstream response{"{\"action\":null}"};
  void setConnectTimeout(unsigned ms) { assert(ms == 1500); }
  void setTimeout(unsigned ms) { assert(ms == 1500); }
  bool begin(WiFiClientSecure&, const String&) { return true; }
  void addHeader(const char*, const String&) {}
  int POST(const String& payload) {
    assert(!payload.isEmpty());
    std::unique_lock<std::mutex> lock(httpMutex); httpStarted = true; httpGate.notify_all();
    httpGate.wait(lock, [] { return httpReleased; }); return httpCode;
  }
  std::istringstream& getStream() { return response; }
  void end() { ++finishedHttp; }
};
#include "../ESP_Timiac/src/cloud_transport.h"

int main() {
  assert(configureCloudTransport());
  CloudExchange* pending = new CloudExchange; pending->payload = "{\"status\":{}}";
  assert(xQueueSend(cloudRequests, &pending, 0) == pdTRUE);
  {
    std::unique_lock<std::mutex> lock(httpMutex);
    assert(httpGate.wait_for(lock, std::chrono::seconds(3), [] { return httpStarted; }));
  }
  // The old synchronous HTTPS call would prevent every one of these ticks.
  // Exercise actual clock scheduling, hourly animations, UART and peer barriers.
  assert(clockSoak(true) == 0);
  assert(!systemLatched && releaseClockEnabled && !finishedHttp);
  CloudExchange* result = nullptr; assert(xQueueReceive(cloudResponses, &result, 0) == 0);
  { std::lock_guard<std::mutex> lock(httpMutex); httpReleased = true; httpGate.notify_all(); }
  assert(xQueueReceive(cloudResponses, &result, portMAX_DELAY) == pdTRUE && result == pending);
  assert(result->error == "Cloud service returned HTTP 503" && result->response.isEmpty()); delete result;
  assert(!liveTls && finishedHttp == 1);
  // A failed cloud attempt cannot poison the next exchange or leak TLS memory.
  httpCode = 200; pending = new CloudExchange; pending->payload = "{\"status\":{}}";
  assert(xQueueSend(cloudRequests, &pending, 0) == pdTRUE);
  assert(xQueueReceive(cloudResponses, &result, portMAX_DELAY) == pdTRUE && result == pending);
  assert(result->error.isEmpty() && result->response == "{\"action\":null}" && !liveTls && finishedHttp == 2); delete result;
  { std::lock_guard<std::mutex> lock(cloudRequests->mutex); cloudRequests->closed = true; cloudRequests->ready.notify_all(); }
  cloudTask->join(); delete cloudTask; vQueueDelete(cloudRequests); vQueueDelete(cloudResponses);
  std::cout << "Production cloud task: blocked HTTPS cannot stall 24 hours of clock motion; failure/retry ownership and TLS cleanup passed\n";
}
