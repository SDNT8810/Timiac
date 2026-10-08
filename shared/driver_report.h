#pragma once
#include <stdlib.h>
#include <string.h>
#include "machine_generated.h"

namespace timiac {
// M122 is parsed independently of the UI log ring; a large diagnostic report
// must never lose its first lines just because only eight events fit a poll.
struct DriverReport {
  // Marlin M122 uses INTERNAL driver labels even though G-code uses A/B/C.
  // This translation is confined to diagnostics; it never changes commands.
  static const char* labels() { return "XYZIJK"; }
  uint8_t connected = 0, tested = 0, settings = 0;
  uint8_t modeOk = 0, currentOk = 0, stepsOk = 0;
  uint8_t thermalWarning = 0, thermalHistory = 0;
  int32_t currentMa[6] = {-1, -1, -1, -1, -1, -1};
  int32_t microsteps[6] = {-1, -1, -1, -1, -1, -1};
  bool columns = false;
  void begin() { *this = DriverReport(); }
  bool good() const { return tested == 63 && connected == 63 && columns && settings == 7 && modeOk == 63 && currentOk == 63 && stepsOk == 63 && !thermalWarning; }
  void receive(const char* line) {
    if (!strncmp(line, "Testing ", 8)) {
      const char* axis = strchr(labels(), line[8]);
      if (!axis || axis == labels() + 6 || strncmp(line + 9, " connection... ", 15)) return;
      const uint8_t bit = 1U << (axis - labels());
      tested |= bit;
      if (!strcmp(line + 24, "OK")) connected |= bit;
      else connected &= ~bit;
      return;
    }
    // Marlin headers and data use tabs. Preserve token boundaries and never
    // interpret a missing column as the next axis' value.
    char copy[192];
    if (strlen(line) >= sizeof(copy)) return;
    strcpy(copy, line);
    char* tokens[8] = {}; unsigned count = 0;
    char* save = nullptr;
    for (char* p = strtok_r(copy, "\t", &save); p && count < 8; p = strtok_r(nullptr, "\t", &save)) tokens[count++] = p;
    if (count == 6 && tokens[0][0] == 'X' && tokens[0][1] == 0) {
      columns = true;
      for (unsigned i = 0; i < 6; ++i) columns &= tokens[i][0] == labels()[i] && tokens[i][1] == 0;
      if (columns) return;
    }
    if (!columns || count != 7) return;
    if (!strcmp(tokens[0], "OT prewarn") || !strcmp(tokens[0], " OTP")) {
      uint8_t& mask = !strcmp(tokens[0], "OT prewarn") ? thermalWarning : thermalHistory;
      mask = 0;
      for (unsigned i = 0; i < 6; ++i)
        if (!strcmp(tokens[i + 1], "true")) mask |= 1U << i;
      return;
    }
    uint8_t* mask = nullptr; unsigned expected = 0;
    if (!strcmp(tokens[0], "Set current")) { mask = &currentOk; expected = driverCurrent; settings |= 1; }
    else if (!strcmp(tokens[0], "msteps")) { mask = &stepsOk; expected = driverMicrosteps; settings |= 2; }
    else if (!strcmp(tokens[0], "stealthChop")) { mask = &modeOk; settings |= 4; }
    if (!mask) return;
    *mask = 0;
    for (unsigned i = 0; i < 6; ++i) {
      char* end = nullptr;
      const unsigned value = strtoul(tokens[i + 1], &end, 10);
      const bool numeric = tokens[i + 1][0] >= '0' && tokens[i + 1][0] <= '9' && !*end && value <= 65535;
      if (mask == &currentOk) currentMa[i] = numeric ? int32_t(value) : -1;
      if (mask == &stepsOk) microsteps[i] = numeric ? int32_t(value) : -1;
      const bool match = mask == &modeOk ? !strcmp(tokens[i + 1], "true") : numeric && value == expected;
      if (match) *mask |= 1U << i;
    }
  }
};
}
