#pragma once
#include <stdint.h>

namespace timiac {
// TLS needs two record buffers plus certificate/handshake storage. Keep room
// for peer traffic and HTTP/Stop handling; total heap alone hides fragmentation.
inline bool networkMemoryReady(uint32_t freeBytes, uint32_t largestBlock, bool secure, bool reservedRecords = false) {
  // Reserved records are outside the heap. Leave 48 KB for the handshake,
  // certificates, peer packets and local HTTP; do not charge the records twice.
  if (secure && reservedRecords) return freeBytes >= 48U * 1024 && largestBlock >= 8U * 1024;
  return secure ? freeBytes >= 96U * 1024 && largestBlock >= 32U * 1024
                : freeBytes >= 24U * 1024 && largestBlock >= 8U * 1024;
}
inline bool cloudConnectionAllowed(bool openingFirmware, bool readingFirmware, bool firmwarePending) {
  return !openingFirmware && !readingFirmware && !firmwarePending;
}
}
