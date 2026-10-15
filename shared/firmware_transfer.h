#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

namespace timiac {
constexpr uint32_t octopusFirmwareLimit = 512UL * 1024 - 32UL * 1024;
constexpr unsigned firmwareBlockBytes = 64;
inline uint32_t firmwareCrc(uint32_t crc, const uint8_t* data, size_t size) {
  while (size--) {
    crc ^= *data++;
    for (unsigned bit = 0; bit < 8; ++bit) crc = (crc >> 1) ^ (0xEDB88320UL & (0UL - (crc & 1)));
  }
  return crc;
}
inline int firmwareHex(char c) {
  return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
}
struct FirmwareCommand {
  unsigned type = 99, count = 0;
  uint32_t offset = 0, crc = 0;
  uint8_t bytes[firmwareBlockBytes] = {};
};
inline bool firmwareNumber(const char*& p, uint32_t& value) {
  if (*p < '0' || *p > '9') return false;
  uint64_t number = 0;
  do { number = number * 10 + (*p++ - '0'); if (number > 0xFFFFFFFFULL) return false; } while (*p >= '0' && *p <= '9');
  value = uint32_t(number); return true;
}
// P0=size/CRC begin; P1=offset/block CRC/hex; P2=verify; P3=activate;
// P4=abort; P5=capability. No filename is supplied by the sender.
inline bool parseFirmwareCommand(const char* p, FirmwareCommand& out) {
  out = FirmwareCommand();
  if (strncmp(p, "M997 P", 6)) return false;
  p += 6; uint32_t type;
  if (!firmwareNumber(p, type) || type > 5) return false;
  out.type = type;
  if (type <= 1) {
    if (*p++ != ' ' || !firmwareNumber(p, out.offset) || *p++ != ' ' || !firmwareNumber(p, out.crc)) return false;
    if (type == 0) return !*p && out.offset >= 1024 && out.offset <= octopusFirmwareLimit;
    if (*p++ != ' ') return false;
    while (*p) {
      if (!p[1] || out.count == firmwareBlockBytes) return false;
      const int high = firmwareHex(*p++), low = firmwareHex(*p++);
      if (high < 0 || low < 0) return false;
      out.bytes[out.count++] = uint8_t(high * 16 + low);
    }
    return out.count && (firmwareCrc(0xFFFFFFFFUL, out.bytes, out.count) ^ 0xFFFFFFFFUL) == out.crc;
  }
  return !*p;
}
inline bool stm32FirmwareHeader(const uint8_t* bytes, size_t size) {
  if (size < 8) return false;
  const uint32_t stack = uint32_t(bytes[0]) | uint32_t(bytes[1]) << 8 | uint32_t(bytes[2]) << 16 | uint32_t(bytes[3]) << 24;
  const uint32_t reset = uint32_t(bytes[4]) | uint32_t(bytes[5]) << 8 | uint32_t(bytes[6]) << 16 | uint32_t(bytes[7]) << 24;
  return stack >= 0x20000000UL && stack <= 0x20020000UL && !(stack & 3) &&
    (reset & 1) && reset >= 0x08008001UL && reset < 0x08080000UL;
}
}
