#include "host/Arduino.h"
#define ARDUINOJSON_ENABLE_ARDUINO_STRING 1
#include <ArduinoJson.h>
#include <esp_heap_caps.h>
#include <cstdlib>
#include <mutex>
#include <thread>
#include <iostream>
using portMUX_TYPE = std::mutex;
#define portMUX_INITIALIZER_UNLOCKED {}
#define portENTER_CRITICAL(mux) (mux)->lock()
#define portEXIT_CRITICAL(mux) (mux)->unlock()
void* (*sdkAllocate)(size_t, size_t) = nullptr;
void (*sdkRelease)(void*) = nullptr;
bool rejectHeap = false;
unsigned heapCalls = 0, liveBlocks = 0;
uint32_t freeByteHeap = 81728;
uint32_t heap_caps_get_free_size(unsigned caps) {
  return caps == MALLOC_CAP_INTERNAL ? 120628 : freeByteHeap;
}
uint32_t heap_caps_get_largest_free_block(unsigned caps) {
  return caps == MALLOC_CAP_INTERNAL ? 38900 : 14000;
}
void* heap_caps_calloc(size_t count, size_t size, unsigned caps) {
  assert(caps == (MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)); ++heapCalls;
  if (rejectHeap) return nullptr;
  const size_t bytes = count * size;
  if (bytes > freeByteHeap) return nullptr;
  auto* block = static_cast<size_t*>(std::calloc(1, bytes + sizeof(size_t)));
  if (!block) return nullptr;
  *block = bytes; freeByteHeap -= bytes; ++liveBlocks;
  return block + 1;
}
void heap_caps_free(void* pointer) {
  if (!pointer) return;
  auto* block = static_cast<size_t*>(pointer) - 1;
  assert(liveBlocks); --liveBlocks; freeByteHeap += *block; std::free(block);
}
int mbedtls_platform_set_calloc_free(void* (*allocate)(size_t, size_t), void (*release)(void*)) {
  assert(!sdkAllocate && !sdkRelease); sdkAllocate = allocate; sdkRelease = release; return 0;
}
#include "../ESP_Timiac/src/tls_memory.h"

int main() {
  configureTlsMemory(); assert(tlsRecordsReady && sdkAllocate && sdkRelease);
  // The old metrics report 120 KB / 38 KB; TLS can actually use 81 KB / 14 KB.
  assert(platformFreeNetworkHeap() == 81728 && platformLargestNetworkBlock() == 14000);
  rejectHeap = true;
  void* input = sdkAllocate(1, MBEDTLS_SSL_IN_BUFFER_LEN);
  void* output = sdkAllocate(MBEDTLS_SSL_OUT_BUFFER_LEN, 1);
  assert(input && output && input != output && !heapCalls);
  assert(!sdkAllocate(1, MBEDTLS_SSL_IN_BUFFER_LEN) && tlsAllocationFailures == 1);
  assert(!sdkAllocate(SIZE_MAX, 2) && !heapCalls); // Overflow cannot wrap to a small allocation.
  assert(!sdkAllocate(1, 2048) && tlsLastFailedAllocationBytes == 2048);
  sdkRelease(input); sdkRelease(output); sdkRelease(nullptr);
  rejectHeap = false;
  void* certificate = sdkAllocate(1, 2048); assert(certificate && liveBlocks == 1);
  sdkRelease(certificate); assert(!liveBlocks);
  // Emulate the overnight peak: a handshake must fail before consuming the
  // last bytes needed by Wi-Fi and the coordinator, and recover after cleanup.
  freeByteHeap = kTlsControlReserve + 2048;
  certificate = sdkAllocate(1, 2048); assert(certificate && freeByteHeap == kTlsControlReserve);
  assert(!sdkAllocate(1, 32) && tlsReserveDenials == 1 && tlsLastFailedAllocationBytes == 32);
  sdkRelease(certificate); assert(freeByteHeap == kTlsControlReserve + 2048);
  freeByteHeap = kTlsControlReserve - 1;
  assert(!sdkAllocate(1, 1) && tlsReserveDenials == 2);
  input = sdkAllocate(1, MBEDTLS_SSL_IN_BUFFER_LEN); assert(input); sdkRelease(input);
  freeByteHeap = 81728;
  // Two tasks may free/acquire records; never alias a live session's buffer.
  auto churn = [] {
    for (unsigned n = 0; n < 1000; ++n) {
      auto* record = static_cast<unsigned char*>(sdkAllocate(1, MBEDTLS_SSL_IN_BUFFER_LEN));
      assert(record); memset(record, 0xa5, MBEDTLS_SSL_IN_BUFFER_LEN); sdkRelease(record);
    }
  };
  std::thread first(churn), second(churn); first.join(); second.join();
  assert(!liveBlocks && tlsAllocationFailures == 4);
  DynamicJsonDocument diagnostics(512); writeTlsMemoryStatus(diagnostics.to<JsonObject>());
  assert(diagnostics["tlsReservedBytes"] == 33434 && diagnostics["tlsAllocationFailures"] == 4);
  assert(diagnostics["tlsLastFailedAllocationBytes"] == 1);
  assert(diagnostics["tlsControlReserveBytes"] == 16384 && diagnostics["tlsReserveDenials"] == 2);
  std::cout << "Production TLS allocator: excludes instruction RAM, records survive failed heap, SDK hooks, concurrent cleanup and allocation diagnostics passed\n";
}
