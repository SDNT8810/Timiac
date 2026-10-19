#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>

namespace timiac {
// Match immutable image markers across arbitrary network chunk boundaries.
// A longer role/revision with the same prefix is not a match.
struct FirmwareMarker {
  char text[64] = {};
  uint8_t fallback[64] = {}, length = 0, matched = 0;
  bool found = false;
  static bool continuation(uint8_t c) {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') ||
      (c >= 'A' && c <= 'Z') || c == '_' || c == '-' || c == '.';
  }
  void begin(const char* value) {
    *this = FirmwareMarker();
    const size_t count = strlen(value);
    if (!count || count >= sizeof(text)) return;
    memcpy(text, value, count); length = count;
    for (unsigned i = 1, j = 0; i < length; ++i) {
      while (j && text[i] != text[j]) j = fallback[j-1];
      if (text[i] == text[j]) ++j;
      fallback[i] = j;
    }
  }
  void write(uint8_t c) {
    if (found || !length) return;
    if (matched == length) {
      if (!continuation(c)) { found = true; return; }
      matched = fallback[matched-1];
    }
    while (matched && c != uint8_t(text[matched])) matched = fallback[matched-1];
    if (c == uint8_t(text[matched])) ++matched;
  }
  bool complete() const { return length && (found || matched == length); }
};
struct FirmwareImageMarkers {
  FirmwareMarker role, machine, revision, recovery;
  bool requireRecovery = false;
  void begin(const char* expectedRole, const char* expectedMachine, const char* expectedRevision, const char* capability = nullptr) {
    role.begin(expectedRole); machine.begin(expectedMachine); revision.begin(expectedRevision);
    requireRecovery = capability != nullptr; recovery.begin(capability ? capability : "");
  }
  void write(const uint8_t* data, size_t count) {
    while (count--) { role.write(*data); machine.write(*data); revision.write(*data); recovery.write(*data++); }
  }
  bool complete() const { return role.complete() && machine.complete() && revision.complete() && (!requireRecovery || recovery.complete()); }
};
}
