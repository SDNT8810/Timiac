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
uint32_t heap_caps_get_free_size(unsigned caps) {
  return caps == MALLOC_CAP_INTERNAL ? 120628 : 81728;
}
uint32_t heap_caps_get_largest_free_block(unsigned caps) {
  return caps == MALLOC_CAP_INTERNAL ? 38900 : 14000;
}
void* heap_caps_calloc(size_t count, size_t size, unsigned caps) {
  assert(caps == (MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)); ++heapCalls;
  if (rejectHeap) return nullptr;
  void* block = std::calloc(count, size); if (block) ++liveBlocks; return block;
}
void heap_caps_free(void* pointer) { if (pointer) { assert(liveBlocks); --liveBlocks; } std::free(pointer); }
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
  // Two tasks may free/acquire records; never alias a live session's buffer.
  auto churn = [] {
    for (unsigned n = 0; n < 1000; ++n) {
      auto* record = static_cast<unsigned char*>(sdkAllocate(1, MBEDTLS_SSL_IN_BUFFER_LEN));
      assert(record); memset(record, 0xa5, MBEDTLS_SSL_IN_BUFFER_LEN); sdkRelease(record);
    }
  };
  std::thread first(churn), second(churn); first.join(); second.join();
  assert(!liveBlocks && tlsAllocationFailures == 2);
  DynamicJsonDocument diagnostics(512); writeTlsMemoryStatus(diagnostics.to<JsonObject>());
  assert(diagnostics["tlsReservedBytes"] == 33434 && diagnostics["tlsAllocationFailures"] == 2);
  assert(diagnostics["tlsLastFailedAllocationBytes"] == 2048);
  std::cout << "Production TLS allocator: excludes instruction RAM, records survive failed heap, SDK hooks, concurrent cleanup and allocation diagnostics passed\n";
}
