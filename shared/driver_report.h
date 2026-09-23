#pragma once
#include <stdlib.h>
#include <string.h>
#include "machine_generated.h"

namespace timiac {
// M122 is parsed independently of the UI log ring; a large diagnostic report
// must never lose its first lines just because only eight events fit a poll.
struct DriverReport {
  uint8_t connected = 0, tested = 0, settings = 0;
  uint8_t modeOk = 0, currentOk = 0, stepsOk = 0;
  bool columns = false;
  void begin() { *this = DriverReport(); }
  bool good() const { return tested == 63 && connected == 63 && columns && settings == 7 && modeOk == 63 && currentOk == 63 && stepsOk == 63; }
  void receive(const char* line) {
    if (!strncmp(line, "Testing ", 8)) {
      const char* axis = strchr(axes, line[8]);
      if (!axis || axis == axes + 6 || strncmp(line + 9, " connection... ", 15)) return;
      const uint8_t bit = 1U << (axis - axes);
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
    if (count == 6) {
      columns = true;
      for (unsigned i = 0; i < 6; ++i) columns &= tokens[i][0] == axes[i] && tokens[i][1] == 0;
      if (columns) return;
    }
    if (!columns || count != 7) return;
    uint8_t* mask = nullptr; unsigned expected = 0;
    if (!strcmp(tokens[0], "Set current")) { mask = &currentOk; expected = driverCurrent; settings |= 1; }
    else if (!strcmp(tokens[0], "msteps")) { mask = &stepsOk; expected = driverMicrosteps; settings |= 2; }
    else if (!strcmp(tokens[0], "stealthChop")) { mask = &modeOk; settings |= 4; }
    if (!mask) return;
    *mask = 0;
    for (unsigned i = 0; i < 6; ++i) {
      char* end = nullptr;
      const unsigned value = strtoul(tokens[i + 1], &end, 10);
      const bool match = mask == &modeOk ? !strcmp(tokens[i + 1], "false") : end != tokens[i + 1] && !*end && value == expected;
      if (match) *mask |= 1U << i;
    }
  }
};
}
