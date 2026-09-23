#pragma once
#include "gcode.h"
#include "endstops.h"
#include "driver_report.h"
#include "timeouts.h"

namespace timiac {
// Deterministic controller kernel. No Arduino, Wi-Fi, allocation, or global
// state. One outstanding UART command; completion is bound to that command.
class OctopusSession {
 public:
  enum class State { Offline, Identify, Drivers, Ready, Prepare, Prepared, Run, Drain, Halted };
  State state = State::Offline;
  DriverReport drivers;
  EndstopReport endstops;
  uint8_t homed = 0;
  float positions[6] = {};
  bool known[6] = {};
  char fault[180] = {}, active[128] = {};
  uint32_t failures = 0, completions = 0, resets = 0;
  bool waiting = false, identity = false;
  bool monitor = false;
  bool homingInFlight = false;
  uint32_t sent = 0, lastRx = 0, lastPoll = 0;
  bool online(uint32_t now) const {
    return identity && state != State::Halted && uint32_t(now - lastRx) < (running() ? 180000U : 15000U);
  }
  bool healthy(uint32_t now) const { return online(now) && drivers.good() && !fault[0]; }
  bool running() const { return state == State::Run; }
  bool prepared() const { return state == State::Prepared && !waiting && drivers.good() && !fault[0]; }
  bool halted() const { return state == State::Halted; }
  bool busy() const { return running() || state == State::Prepare || state == State::Prepared || state == State::Drain; }
  void clearPosition() { homed = 0; for (auto& value : known) value = false; }
  bool stage(const char* const* lines, unsigned count, uint32_t now) {
    if (state != State::Ready || !healthy(now) || count > 6) return false;
    size = cursor = 0; requireDriverCheck = false;
    add("G21"); add("G90");
    for (unsigned n = 0; n < count; ++n) {
      if (!validNative(lines[n], axisCount, travelMm, maxFeed)) { size = 0; return false; }
      if (homingCommand(lines[n])) {
        requireDriverCheck = true;
        // Each acknowledgement names the exact axis which has finished.
        // A failing A can no longer hide whether B/C were ever attempted.
        for (unsigned a = 0; a < 6; ++a) if (strchr(lines[n] + 4, axes[a])) {
          char one[] = "G28 X"; one[4] = axes[a]; if (!add(one)) return false;
        }
      } else {
        if (!strncmp(lines[n], "G1 ", 3) && (moveMask(lines[n]) & homed) != moveMask(lines[n])) { size = 0; return false; }
        if (!add(lines[n])) return false;
      }
    }
    if (!add("M400") || !add("M114")) return false;
    state = State::Prepare;
    return true;
  }
  bool commit() {
    if (!prepared()) return false;
    state = State::Run; endstops.clear(); return true;
  }
  // Returns the immediate command to transmit, or null. Homing is a blocking
  // Marlin routine: M410 alone does not cancel its subsequent bump/move calls.
  // A homing emergency therefore uses M112 and truthfully requires board reset.
  const char* stop(uint32_t now) {
    const bool homing = homingInFlight;
    size = cursor = 0; clearPosition(); endstops.clear();
    if (state == State::Halted) return nullptr;
    if (homing) {
      fail("Homing stopped. Reset this Octopus before continuing.", true);
      return "M112";
    }
    // Drain all old acknowledgements. A unique M118 marker must be observed
    // before a fresh handshake can use an ok. Never treat a late G28 ok as M115.
    state = State::Drain; waiting = false; identity = false; lastRx = sent = now;
    return "M410\nM400\nM118 TIMIAC_DRAINED";
  }
  void fail(const char* message, bool hard = false) {
    if (!fault[0]) { snprintf(fault, sizeof(fault), "%s", message); ++failures; }
    size = cursor = 0; clearPosition(); endstops.clear(); waiting = false;
    state = hard ? State::Halted : State::Offline;
    if (hard) identity = false;
  }
  const char* next(uint32_t now) {
    if (state == State::Halted) return nullptr;
    if (state == State::Drain) {
      if (uint32_t(now - sent) >= 10000) fail("Stop acknowledgement missing; check Octopus connection", true);
      return nullptr;
    }
    if (waiting) {
      const uint32_t limit = running() ? acknowledgementTimeout(active) : 10000;
      if (uint32_t(now - sent) >= limit) fail(homingCommand(active) ? "Homing acknowledgement timed out" : "Octopus acknowledgement timed out");
      return nullptr;
    }
    if (state == State::Offline) {
      if (now < 3000 || uint32_t(now - lastPoll) < 1000) return nullptr;
      state = State::Identify; identity = false; lastPoll = now;
      return transmit("M115", now);
    }
    if (state == State::Drivers) { drivers.begin(); return transmit("M122", now); }
    if (state == State::Prepare) {
      if (requireDriverCheck) { requireDriverCheck = false; drivers.begin(); return transmit("M122", now); }
      state = State::Prepared; return nullptr;
    }
    if (state == State::Run) {
      if (cursor < size) return transmit(queue[cursor++], now);
      state = State::Ready; ++completions; lastPoll = now; return nullptr;
    }
    if (state == State::Ready && uint32_t(now - lastPoll) >= (monitor ? 500U : 5000U)) {
      lastPoll = now;
      // Switches remain available with a driver fault; failed drivers retry
      // automatically, but no movement is automatically replayed.
      if (!drivers.good() && uint32_t(now - driverAt) >= 5000) { drivers.begin(); return transmit("M122", now); }
      return transmit("M119", now);
    }
    return nullptr;
  }
  void receive(const char* line, uint32_t now) {
    if (!strcmp(line, "start")) {
      const bool interrupted = busy();
      state = State::Offline; identity = waiting = false; drivers.begin(); clearPosition(); endstops.clear();
      homingInFlight = false;
      fault[0] = 0; size = cursor = 0; lastPoll = now; ++resets;
      if (interrupted) { snprintf(fault, sizeof(fault), "Octopus restarted during work"); ++failures; }
      return;
    }
    if (strstr(line, "Printer halted") || strstr(line, "kill() called")) { fail(line, true); return; }
    if (!strncmp(line, "echo:Bad ", 9) && strstr(line, " Endstop?")) { fail(line, true); return; }
    if (state == State::Halted) return;
    if (state == State::Drain) {
      if (!strcmp(line, "TIMIAC_DRAINED") || !strcmp(line, "echo:TIMIAC_DRAINED")) {
        state = State::Offline; waiting = false; lastPoll = now; fault[0] = 0;
      }
      return;
    }
    if (!strncmp(line, "Error:", 6) || !strncmp(line, "!!", 2) || !strncmp(line, "Resend:", 7) ||
        strstr(line, "Unknown command") || (strstr(line, "Home ") && strstr(line, " First"))) {
      fail(line); return;
    }
    if (!waiting) return;
    lastRx = now;
    if (!strcmp(active, "M115") && strstr(line, "FIRMWARE_NAME:"))
      identity = strstr(line, machineId) && strstr(line, "AXIS_COUNT:6") &&
        !strstr(line, "AXIS_COUNT:60");
    if (!strcmp(active, "M122")) drivers.receive(line);
    if (!strcmp(active, "M119")) endstops.receive(line);
    if (!strcmp(active, "M114") || homingCommand(active)) readPosition(line);
    if (strcmp(line, "ok") && strncmp(line, "ok ", 3)) return;
    waiting = false;
    homingInFlight = false;
    if (state == State::Identify) {
      if (!identity) { fail("Wrong Octopus firmware: flash the matching TIMIAC2 image"); return; }
      state = State::Drivers;
    } else if (!strcmp(active, "M122")) {
      driverAt = now;
      if (!drivers.good()) {
        snprintf(fault, sizeof(fault), "Driver check failed: connection/current/microsteps/mode; see driver panel");
        if (state == State::Prepare || state == State::Run) { ++failures; size = cursor = 0; clearPosition(); }
        state = State::Ready;
      } else {
        fault[0] = 0;
        if (state == State::Drivers) state = State::Ready;
      }
    } else if (!strcmp(active, "M119")) endstops.finish(now);
    if (running() && homingCommand(active)) {
      const uint8_t mask = moveMask(active);
      homed |= mask;
      for (unsigned a = 0; a < 6; ++a) if (mask & (1U << a)) { known[a] = true; positions[a] = 0; }
    }
    if (running() && (!strcmp(active, "M18") || !strcmp(active, "M84"))) clearPosition();
  }
  static uint8_t moveMask(const char* line) {
    uint8_t mask = 0;
    for (unsigned a = 0; a < 6; ++a) if (strchr(line, axes[a])) mask |= 1U << a;
    return mask;
  }
 private:
  char queue[44][128] = {};
  unsigned size = 0, cursor = 0;
  bool requireDriverCheck = false;
  uint32_t driverAt = 0;
  bool add(const char* line) {
    if (size >= 44 || strlen(line) >= 128) { size = 0; return false; }
    strcpy(queue[size++], line); return true;
  }
  const char* transmit(const char* command, uint32_t now) {
    snprintf(active, sizeof(active), "%s", command); waiting = true; sent = now;
    homingInFlight = homingCommand(active);
    if (!strcmp(active, "M119")) endstops.begin();
    return active;
  }
  void readPosition(const char* line) {
    for (unsigned a = 0; a < 6; ++a) {
      char key[] = "X:"; key[0] = axes[a];
      const char* p = strstr(line, key);
      if (!p || (p != line && p[-1] != ' ')) continue;
      char* end = nullptr; const float value = strtof(p + 2, &end);
      if (end != p + 2 && isfinite(value)) { known[a] = true; positions[a] = value; }
    }
  }
};
}
