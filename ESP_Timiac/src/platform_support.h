#pragma once

// Master uses I2C for the DS3231 and GPIO27 for the divided photoelectric input.
// Existing ultrasonic PCB traces on GPIO18/19 are left unused.
// It has no Octopus UART connection.
// ESP-12S bridges alone initialize the Octopus control UART.
#if TIMIAC_MASTER && !defined(ARDUINO_ARCH_ESP32)
#error "Build the Timiac master for ESP32"
#elif !TIMIAC_MASTER && !defined(ARDUINO_ARCH_ESP8266)
#error "Build the Octopus bridges for ESP8266 / ESP-12S"
#endif

#if defined(ARDUINO_ARCH_ESP32)
#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <AsyncJson.h>
#include <esp_system.h>
#include <lwip/sockets.h>
#include <errno.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include "../../shared/network_memory.h"
#include "tls_memory.h"

inline bool platformNetworkMemoryReady(bool secure) {
  // ESP.getFreeHeap()/getMaxAllocHeap() also count 32-bit-only instruction
  // RAM. TLS and JSON need byte-addressable internal RAM.
  return timiac::networkMemoryReady(platformFreeNetworkHeap(), platformLargestNetworkBlock(), secure, tlsRecordsReady);
}

// AsyncTCP HTTP callbacks and Arduino loop() execute in different tasks.
// A recursive mutex also covers status replies nested inside API handlers.
SemaphoreHandle_t runtimeMutex = nullptr;
struct RuntimeLock {
  RuntimeLock() { xSemaphoreTakeRecursive(runtimeMutex, portMAX_DELAY); }
  ~RuntimeLock() { xSemaphoreGiveRecursive(runtimeMutex); }
  RuntimeLock(const RuntimeLock&) = delete;
  RuntimeLock& operator=(const RuntimeLock&) = delete;
};
inline uint32_t platformRandom() { return esp_random(); }
inline String platformChipId() {
  uint64_t mac = ESP.getEfuseMac();
  return String(uint32_t(mac >> 32), HEX) + String(uint32_t(mac), HEX);
}
#else
#include <ESP8266WiFi.h>
inline uint32_t platformRandom() { return os_random(); }
inline String platformChipId() { return String(ESP.getChipId(), HEX); }
#endif

// Return accepted bytes, zero for temporary congestion, or -1 for a broken link.
// ESP8266 writes are limited to current buffer space and its short timeout.
inline int writePeerChunk(WiFiClient& socket, const char* data, size_t length) {
#if defined(ARDUINO_ARCH_ESP32)
  // ESP32's inherited availableForWrite() reports zero, and WiFiClient::write
  // can retry for seconds. Never let a stalled bridge block health/Stop traffic.
  const int written = lwip_send(socket.fd(), data, length, MSG_DONTWAIT);
  if (written >= 0) return written;
  return errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR || errno == ENOBUFS || errno == ENOMEM ? 0 : -1;
#else
  const int available = socket.availableForWrite();
  if (available <= 0) return socket.connected() ? 0 : -1;
  if (length > size_t(available)) length = size_t(available);
  const size_t written = socket.write(reinterpret_cast<const uint8_t*>(data), length);
  return written ? int(written) : socket.connected() ? 0 : -1;
#endif
}
