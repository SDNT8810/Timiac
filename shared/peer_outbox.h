#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>

namespace timiac {
// A bounded TCP byte stream. A short write advances only the accepted prefix;
// later loop iterations send the suffix. Frames must never be interleaved.
struct PeerOutbox {
  static constexpr size_t capacity = 4096, maxPacket = 2601;
  char bytes[capacity];
  size_t begin = 0, end = 0;
  uint32_t progressAt = 0;
  bool partialFrame = false;
  size_t pending() const { return end - begin; }
  const char* data() const { return bytes + begin; }
  void clear() { begin = end = 0; partialFrame = false; }
  bool append(const char* packet, size_t length, uint32_t now) {
    if (!length || length > maxPacket || packet[length - 1] != '\n' || length > capacity - pending()) return false;
    if (!pending()) progressAt = now;
    if (end + length > capacity) {
      const size_t remaining = pending();
      memmove(bytes, data(), remaining); begin = 0; end = remaining;
    }
    memcpy(bytes + end, packet, length); end += length; return true;
  }
  void advance(size_t count, uint32_t now) {
    if (!count || count > pending()) return;
    partialFrame = bytes[begin + count - 1] != '\n';
    begin += count; progressAt = now;
    if (begin == end) begin = end = 0;
  }
  bool stalled(uint32_t now, uint32_t timeout) const {
    return pending() && uint32_t(now - progressAt) >= timeout;
  }
};
}
