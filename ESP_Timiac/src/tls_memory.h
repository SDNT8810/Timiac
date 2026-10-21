#pragma once
#include <esp_heap_caps.h>
#include <mbedtls/platform.h>
#include <mbedtls/ssl_internal.h>
#include "../../shared/tls_record_buffers.h"

// Use the actual buffer sizes from the pinned SDK, including TLS overhead.
// Install once before Wi-Fi/tasks start; never swap allocators around a request.
timiac::TlsRecordBuffers<MBEDTLS_SSL_IN_BUFFER_LEN, MBEDTLS_SSL_OUT_BUFFER_LEN> tlsRecords;
portMUX_TYPE tlsMemoryMux = portMUX_INITIALIZER_UNLOCKED;
uint32_t tlsAllocationFailures = 0, tlsLastFailedAllocationBytes = 0;
uint32_t tlsReserveDenials = 0;
// TLS is optional; peer receive buffers, Wi-Fi and Stop handling are not.
// Check every certificate/crypto allocation, not only the start of a handshake.
constexpr uint32_t kTlsControlReserve = 16U * 1024;
bool tlsRecordsReady = false;
inline uint32_t platformFreeNetworkHeap() {
  return heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
}
inline uint32_t platformLargestNetworkBlock() {
  return heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
}
void* allocateTlsMemory(size_t count, size_t size) {
  if (size && count > SIZE_MAX / size) return nullptr;
  const size_t bytes = count * size;
  void* memory;
  if (tlsRecords.matches(bytes)) {
    portENTER_CRITICAL(&tlsMemoryMux);
    memory = tlsRecords.allocate(bytes);
    portEXIT_CRITICAL(&tlsMemoryMux);
  } else {
    const uint32_t available = platformFreeNetworkHeap();
    if (bytes && (available < kTlsControlReserve || bytes > available - kTlsControlReserve)) {
      memory = nullptr;
      portENTER_CRITICAL(&tlsMemoryMux);
      ++tlsReserveDenials;
      portEXIT_CRITICAL(&tlsMemoryMux);
    } else memory = heap_caps_calloc(count, size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  }
  if (!memory && bytes) {
    portENTER_CRITICAL(&tlsMemoryMux);
    ++tlsAllocationFailures; tlsLastFailedAllocationBytes = bytes;
    portEXIT_CRITICAL(&tlsMemoryMux);
  }
  return memory;
}
void freeTlsMemory(void* memory) {
  portENTER_CRITICAL(&tlsMemoryMux);
  const bool reserved = tlsRecords.release(memory);
  portEXIT_CRITICAL(&tlsMemoryMux);
  if (!reserved) heap_caps_free(memory);
}
void configureTlsMemory() {
  tlsRecordsReady = mbedtls_platform_set_calloc_free(allocateTlsMemory, freeTlsMemory) == 0;
}
void writeTlsMemoryStatus(JsonObject status) {
  portENTER_CRITICAL(&tlsMemoryMux);
  const uint32_t failures = tlsAllocationFailures, bytes = tlsLastFailedAllocationBytes, denials = tlsReserveDenials;
  portEXIT_CRITICAL(&tlsMemoryMux);
  status["tlsReservedBytes"] = tlsRecordsReady ? MBEDTLS_SSL_IN_BUFFER_LEN + MBEDTLS_SSL_OUT_BUFFER_LEN : 0;
  status["tlsAllocationFailures"] = failures; status["tlsLastFailedAllocationBytes"] = bytes;
  status["tlsControlReserveBytes"] = kTlsControlReserve; status["tlsReserveDenials"] = denials;
}
