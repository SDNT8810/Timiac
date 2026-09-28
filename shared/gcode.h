#pragma once
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <initializer_list>
#include "machine_generated.h"

namespace timiac {
// Dashboard/program letters and native Octopus G-code letters are identical.
// The master selects the board using the 1/2 suffix; bridges keep XYZABC intact.
struct GcodeState {
  bool absolute = true;
  float feed = maxFeed < 180 ? maxFeed : 180, position[12] = {};
  bool known[12] = {};
};
struct Step {
  char lines[2][3][128] = {};
  unsigned count[2] = {};
  void add(unsigned board, const char* line) {
    if (count[board] < 3) snprintf(lines[board][count[board]++], 128, "%s", line);
  }
  void both(const char* line) { add(0, line); add(1, line); }
};
inline void skipSpace(const char*& p) { while (*p == ' ' || *p == '\t') ++p; }
inline bool readNumber(const char*& p, float& value) {
  skipSpace(p);
  const char* begin = p;
  if (*p == '+' || *p == '-') ++p;
  bool digit = false;
  while (isdigit(*p)) { digit = true; ++p; }
  if (*p == '.') { ++p; while (isdigit(*p)) { digit = true; ++p; } }
  if (!digit) return false;
  value = strtof(begin, nullptr);
  return isfinite(value);
}
// Board suffix and coordinate MUST be separated: X1 10, never ambiguous X110.
// Output moves are explicit G90 + G1; source modal state stays on the master.
// These are actuator coordinates: home/open = 0, closed = travel. Already
// inverted animation files must pass through unchanged to both Octopus boards.
inline const char* compileLine(const char* input, GcodeState& state, Step& result,
                               unsigned axisCount = 1, float travel = 48, float maxFeed = 369.6f) {
  result = Step();
  if (!axisCount || axisCount > 6 || strlen(input) > 240) return "Line/configuration too long or invalid";
  char cleaned[241]; unsigned n = 0; bool comment = false;
  for (const char* s = input; *s; ++s) {
    if (*s == ';' && !comment) break;
    if (*s == '(') { if (comment) return "Nested comment"; comment = true; continue; }
    if (*s == ')') { if (!comment) return "Unexpected closing comment"; comment = false; continue; }
    if (*s == '\n' || *s == '\r' || (*s < 32 && *s != '\t') || *s > 126) return "One ASCII command per line";
    if (!comment) cleaned[n++] = toupper(*s);
  }
  if (comment) return "Unclosed comment";
  cleaned[n] = 0;
  const char* p = cleaned; skipSpace(p);
  if (!*p) return nullptr;
  char kind = *p++;
  if ((kind != 'G' && kind != 'M') || !isdigit(*p)) return "Expected a G or M command";
  unsigned code = 0;
  while (isdigit(*p)) { code = code * 10 + (*p++ - '0'); if (code > 999) return "Unknown command"; }
  skipSpace(p);
  GcodeState next = state;
  if (kind == 'M') {
    if (*p || (code != 114 && code != 115 && code != 119 && code != 122 && code != 400 && code != 18 && code != 84))
      return "Supported diagnostics: M114 M115 M119 M122 M400; release: M18/M84";
    char command[12]; snprintf(command, sizeof(command), "M%u", code); result.both(command);
    if (code == 18 || code == 84) for (bool& k : next.known) k = false;
    state = next; return nullptr;
  }
  if (code == 21) { if (*p) return "G21 takes no parameters"; result.both("G21"); return nullptr; }
  if (code == 4) {
    if (!*p) return "Missing dwell parameter";
    const char unit = *p++; float value;
    if ((unit != 'P' && unit != 'S') || !readNumber(p, value)) return "Use G4 Pmilliseconds or Sseconds";
    skipSpace(p); value *= unit == 'S' ? 1000 : 1;
    if (*p || value < 0 || value > 120000) return "Dwell must be 0-120 seconds";
    char command[40]; snprintf(command, sizeof(command), "G4 P%.0f", value); result.both(command); return nullptr;
  }
  if (code != 0 && code != 1 && code != 28 && code != 90 && code != 91) return "Unsupported G command";
  if (code == 90 || code == 91) next.absolute = code == 90;
  bool selected[12] = {}, any = false, feedSeen = false;
  float values[12] = {};
  while (*p) {
    skipSpace(p); if (!*p) break;
    if (*p == 'F' && code != 28) {
      ++p;
      if (feedSeen || !readNumber(p, next.feed) || next.feed <= 0 || next.feed > maxFeed) return "Invalid/repeated feed or feed exceeds machine limit";
      feedSeen = true;
    } else {
      const char* axis = strchr(axes, *p++);
      if (!axis || axis == axes + 6 || (*p != '1' && *p != '2')) return "Use board-qualified axes X1 ... C1, X2 ... C2";
      unsigned a = axis - axes, board = *p++ - '1', index = board * 6 + a;
      if (a >= axisCount) return "Axis not installed in this build";
      if (selected[index]) return "Repeated axis";
      selected[index] = any = true;
      if (code == 28) { if (*p && *p != ' ' && *p != '\t') return "Separate home axes with spaces"; }
      else {
        if (*p != ' ' && *p != '\t') return "Separate axis name and value with a space: X1 10";
        if (!readNumber(p, values[index])) return "Missing/invalid axis value";
      }
    }
    if (*p && *p != ' ' && *p != '\t') return "Separate parameters with spaces";
  }
  if (code == 28) {
    for (unsigned b = 0; b < 2; ++b) {
      char line[32] = "G28";
      for (unsigned a = 0; a < axisCount; ++a) if (!any || selected[b * 6 + a]) {
        unsigned len = strlen(line); snprintf(line + len, sizeof(line) - len, " %c", axes[a]);
        next.position[b * 6 + a] = 0; next.known[b * 6 + a] = true;
      }
      if (strlen(line) > 3) result.add(b, line);
    }
  } else if (any) {
    for (unsigned b = 0; b < 2; ++b) {
      char line[128] = "G1";
      for (unsigned a = 0; a < axisCount; ++a) {
        unsigned i = b * 6 + a;
        if (!selected[i]) continue;
        if (!next.absolute && !next.known[i]) return "Relative move needs homing or a known absolute position";
        float target = next.absolute ? values[i] : next.position[i] + values[i];
        if (target < 0 || target > travel) return "Target outside machine travel";
        next.position[i] = target; next.known[i] = true;
        unsigned len = strlen(line); snprintf(line + len, sizeof(line) - len, " %c%.3f", axes[a], target);
      }
      if (strlen(line) > 2) {
        unsigned len = strlen(line); snprintf(line + len, sizeof(line) - len, " F%.3f", next.feed);
        result.add(b, "G90"); result.add(b, line);
      }
    }
  } else result.both("G90"); // Mode/feed-only step, without movement.
  state = next; return nullptr;
}

// Gate every native UART line, including stored/uploaded command arrays.
inline bool validNative(const char* line, unsigned axisCount, float travel, float maxFeed) {
  if (strlen(line) > 127) return false;
  for (const char* p = line; *p; ++p) if (*p < 32 || *p > 126) return false;
  for (const char* literal : {"G21", "G90", "M114", "M115", "M119", "M122", "M400", "M18", "M84"})
    if (!strcmp(line, literal)) return true;
  if (!strncmp(line, "G4 P", 4)) {
    const char* p = line + 4; float value;
    return readNumber(p, value) && !*p && value >= 0 && value <= 120000;
  }
  bool home = !strncmp(line, "G28 ", 4), move = !strncmp(line, "G1 ", 3);
  if (!home && !move) return false;
  const char* p = line + (home ? 4 : 3); bool seen[7] = {}, any = false, feed = false;
  while (*p) {
    skipSpace(p); if (!*p) break;
    char key = *p++; const char* axis = strchr(axes, key);
    unsigned index = key == 'F' ? 6 : axis ? unsigned(axis - axes) : 7;
    if (index > 6 || (index != 6 && index >= axisCount) || seen[index] || (home && index == 6)) return false;
    seen[index] = true;
    if (index != 6) any = true;
    if (!home) {
      float value;
      if (!readNumber(p, value) || value < 0 || value > (index == 6 ? maxFeed : travel) || (index == 6 && value == 0)) return false;
      if (index == 6) feed = true;
    }
    if (*p && *p != ' ') return false;
  }
  return any && (home || feed);
}
} // namespace timiac
