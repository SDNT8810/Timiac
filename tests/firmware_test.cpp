#include "../shared/gcode.h"
#include "../shared/safety.h"
#include "../shared/timeouts.h"
#include "../shared/clock.h"
#include "../shared/presets.h"
#include "../shared/proximity.h"
#include <assert.h>
#include <string>
#include <iostream>
#include <fstream>
#ifdef NDEBUG
#error "Firmware regression tests require enabled assertions"
#endif
using namespace timiac;
int main(int argc, char** argv) {
  assert(nextClockMinute(13 * 60 + 58, true) == 14 * 60);
  assert(nextClockMinute(13 * 60 + 55, true) == 14 * 60);
  assert(nextClockMinute(23 * 60 + 59, true) == 0);
  assert(nextClockMinute(23 * 60 + 35, false) == 35);
  assert(sonarMillimetres(0) == -1);
  assert(sonarMillimetres(1159) == -2);
  assert(sonarMillimetres(1160) == 200);
  assert(sonarMillimetres(2900) == 500);
  assert(sonarMillimetres(46401) == -2);
  ProximityGate gate;
  for (int i = 0; i < 20; ++i) assert(!gate.sample(300, true)); // No approach at boot.
  assert(!gate.sample(700, true)); assert(!gate.sample(700, true)); assert(!gate.armed);
  assert(!gate.sample(700, true)); assert(gate.armed);
  assert(!gate.sample(300, true)); assert(!gate.sample(-2, true)); // Reject a noisy echo.
  assert(!gate.sample(499, true)); assert(!gate.sample(499, true)); assert(gate.sample(499, true));
  for (int i = 0; i < 20; ++i) assert(!gate.sample(300, true)); // Held hand does not repeat.
  for (int i = 0; i < 20; ++i) assert(!gate.sample(550, true)); // Hysteresis.
  assert(!gate.armed);
  for (int i = 0; i < 9; ++i) assert(!gate.sample(-1, true));
  assert(!gate.armed); assert(!gate.sample(-1, true)); assert(gate.armed);
  for (int i = 0; i < 20; ++i) assert(!gate.sample(500, true)); // Strictly below 50 cm.
  assert(!gate.sample(300, true)); assert(!gate.sample(300, true));
  assert(!gate.sample(300, false)); assert(!gate.armed); // Work/fault cancels pending gesture.
  for (int i = 0; i < 20; ++i) assert(!gate.sample(300, true));
  for (int i = 0; i < 3; ++i) assert(!gate.sample(650, true));
  assert(!gate.sample(200, true)); assert(!gate.sample(200, true)); assert(gate.sample(200, true));
  for (Preset preset : {Preset::Struggle, Preset::Hour, Preset::Curious}) {
    unsigned moves = 0, dwells = 0;
    float positions[12] = {};
    for (unsigned i = 0; i < presetCount(preset); ++i) {
      Step frame;
      assert(presetStep(preset, i, 3 * 60 + 30, 48, 700, frame));
      for (unsigned b = 0; b < 2; ++b) for (unsigned j = 0; j < frame.count[b]; ++j) {
        const char* line = frame.lines[b][j];
        assert(validNative(line, 6, 48, 700));
        if (!strncmp(line, "G4 ", 3)) ++dwells;
        if (strncmp(line, "G1 ", 3)) continue;
        ++moves;
        for (unsigned a = 0; a < 6; ++a) {
          const char* word = strchr(line, axes[a]);
          if (word) positions[b * 6 + a] = strtof(word + 1, nullptr);
        }
      }
    }
    assert(moves >= 20);
    if (preset == Preset::Struggle) {
      assert(dwells == 4); // Both boards observe both source-file dwells.
      for (float position : positions) assert(fabsf(position - 43.2f) < .001f);
    } else if (preset == Preset::Hour) {
      assert(dwells == 0);
      for (unsigned i = 0; i < 12; ++i) assert(positions[i] == (i == 6 ? 0 : i == 3 ? 24 : 48));
    } else {
      assert(dwells == 16);
      for (unsigned i = 0; i < 12; ++i) assert(fabsf(positions[i] - (i == 6 ? 41.28f : 44.16f)) < .001f);
    }
    Step invalid; assert(!presetStep(preset, presetCount(preset), 0, 48, 700, invalid));
  }
  assert(validClockDate(2024, 2, 29, 23, 59, 59));
  assert(!validClockDate(2025, 2, 29, 0, 0, 0));
  assert(!validClockDate(2026, 4, 31, 0, 0, 0));
  assert(!validClockDate(2100, 1, 1, 0, 0, 0));
  assert(!validClockDate(2026, 0, 1, 0, 0, 0));
  assert(!validClockDate(2026, 1, 1, 24, 0, 0));
  // Every clock pose must pass the same native gate used by both bridges.
  for (unsigned hour = 0; hour < 24; ++hour) for (unsigned minute = 0; minute < 60; ++minute) {
    Step pose;
    assert(userAction("show-time", hour, minute, 48, 700, pose));
    for (unsigned b = 0; b < 2; ++b) {
      for (unsigned n = 0; n < pose.count[b]; ++n) assert(validNative(pose.lines[b][n], 6, 48, 700));
      for (unsigned a = 0; a < 6; ++a) {
        const char* axis = strchr(pose.lines[b][1], axes[a]); assert(axis);
        const unsigned index = b * 6 + a;
        assert(strtof(axis + 1, nullptr) == (index == minute / 5 ? 0 : index == hour % 12 ? 24 : 48));
      }
    }
  }
  for (const char* action : {"home", "open", "close"}) {
    Step pose; assert(userAction(action, 0, 0, 48, 700, pose));
    for (unsigned b = 0; b < 2; ++b) for (unsigned n = 0; n < pose.count[b]; ++n)
      assert(validNative(pose.lines[b][n], 6, 48, 700));
    if (!strcmp(action, "home")) assert(!strcmp(pose.lines[1][0], "G28 X Y Z A B C"));
    else for (unsigned b = 0; b < 2; ++b) for (char axis : std::string(axes))
      assert(strtof(strchr(pose.lines[b][1], axis) + 1, nullptr) == (!strcmp(action, "open") ? 0 : 48));
  }
  // Every hardware example uses the native coordinate system. Generated files
  // must produce exactly the same UART moves as built-in animations (one inversion).
  assert(argc == 2);
  const char* examples[] = {"circular-struggle", "hour-movement", "curious-creature", "small-wave",
                            "two-motor-smoke", "two-motor-example", "board-check"};
  const Preset presets[] = {Preset::Struggle, Preset::Hour, Preset::Curious};
  for (unsigned file = 0; file < 7; ++file) {
    std::ifstream input(std::string(argv[1]) + "/" + examples[file] + ".gcode");
    assert(input.good());
    GcodeState parser; std::string line; unsigned frame = 0;
    while (std::getline(input, line)) {
      if (!line.empty() && line.back() == '\r') line.pop_back();
      Step actual;
      const char* error = compileLine(line.c_str(), parser, actual, 6, 48, 700);
      if (error) std::cerr << examples[file] << ": " << line << ": " << error << "\n";
      assert(!error);
      for (unsigned b = 0; b < 2; ++b) for (unsigned i = 0; i < actual.count[b]; ++i)
        assert(validNative(actual.lines[b][i], 6, 48, 700));
      if (file < 3 && (line.find("G1 ") == 0 || line.find("G4 ") == 0)) {
        Step expected; assert(presetStep(presets[file], frame++, 60, 48, 700, expected));
        assert(!memcmp(&expected, &actual, sizeof(Step)));
      }
    }
    if (file < 3) assert(frame == presetCount(presets[file]));
  }
  Step rejected;
  assert(!userAction("unknown", 0, 0, 48, 700, rejected));
  assert(!userAction("show-time", 24, 0, 48, 700, rejected));
  GcodeState state; Step step;
  auto parse = [&](const char* line) { const char* e = compileLine(line, state, step); if (e) std::cerr << line << ": " << e << "\n"; assert(!e); };
  parse("G90 X1 10 X2 35 F350");
  assert(step.count[0] == 2 && step.count[1] == 2);
  assert(!strcmp(step.lines[0][1], "G1 X10.000 F350.000"));
  assert(!strcmp(step.lines[1][1], "G1 X35.000 F350.000"));
  parse("G91 X2 -5");
  assert(step.count[0] == 0 && !strcmp(step.lines[1][1], "G1 X30.000 F350.000"));
  for (const char* invalid : {"G1 X110", "G1 X1", "G1 X3 1", "G1 Y1 1", "G1 X1 1 X1 2", "G1 X1 100",
      "G90 X1 -1", "G1 X1 NAN", "G1 X1 1 F0", "G1 X1 1 F370", "G1 X1 1 F1 F2",
      "G1 X1 1 M112", "G1 X1 1*23", "G28 X1 10", "G4", "G4 S121", "G4 P1 S1",
      "G1 X1 1\nM112", "G2 X1 1", "M112", "M211 S0", "G92 X1 0", "(bad", "G1 X1 1 (nested(x))"}) {
    auto before = state;
    assert(compileLine(invalid, state, step));
    assert(!memcmp(&before, &state, sizeof(state))); // Failed line cannot corrupt modal state.
  }
  state = GcodeState();
  parse("G28 X1");
  assert(!strcmp(step.lines[0][0], "G28 X") && step.count[1] == 0);
  assert(compileLine("G91 X2 1", state, step));
  parse("G28 X1 X2");
  parse("G91");
  parse("G1 X1 1 X2 2 F180");
  assert(state.position[0] == 1 && state.position[6] == 2);
  parse("G1 X1 2");
  assert(state.position[0] == 3);
  parse("M18"); assert(!state.known[0] && !state.known[6]);
  parse("; comment"); assert(step.count[0] == 0 && step.count[1] == 0);
  state = GcodeState();
  assert(!compileLine("G90 X1 1 Y1 2 Z1 3 A1 4 B1 5 C1 6 X2 7 Y2 8 Z2 9 A2 10 B2 11 C2 12 F350", state, step, 6));
  assert(strstr(step.lines[0][1], "C6.000") && strstr(step.lines[1][1], "C12.000"));
  for (const char* good : {"G21", "G90", "G28 X", "G1 X10.000 F350.000", "M119", "M122", "G4 P500"})
    assert(validNative(good, 1, 48, 369.6f));
  for (const char* bad : {"G91", "G28", "G28 Y", "G1 Y1 F180", "G1 X49 F180", "G1 X1 F370",
      "G1 X1 F180 M112", "G1 X1", "M211 S0", "G28 X1", "M112", "G1 X1\nM112", "G1 X1 X2 F180"})
    assert(!validNative(bad, 1, 48, 369.6f));
  // No malformed random input may produce an unsafe UART line.
  uint32_t rng = 42;
  for (int i = 0; i < 30000; ++i) {
    std::string input;
    for (unsigned j = 0, size = (rng % 100); j < size; ++j) {
      rng = rng * 1664525U + 1013904223U;
      input += char(32 + (rng % 95));
    }
    GcodeState fuzz;
    if (!compileLine(input.c_str(), fuzz, step))
      for (unsigned b = 0; b < 2; ++b) for (unsigned n = 0; n < step.count[b]; ++n)
        assert(validNative(step.lines[b][n], 1, 48, 369.6f));
  }
  // G28 remains valid past 60/120 seconds and expires at 180, including millis wrap.
  for (const char* line : {"G28 X", "G28 X Y Z A B C"}) {
    assert(homingCommand(line));
    const uint32_t started = 0xfffffff0U;
    assert(!expired(started + 120001U, started, acknowledgementTimeout(line)));
    assert(!expired(started + 179999U, started, acknowledgementTimeout(line)));
    assert(expired(started + 180000U, started, acknowledgementTimeout(line)));
  }
  for (const char* line : {"G1 X10 F350", "G4 P120000", "M400", "G280", "G28.1", ""}) {
    assert(!homingCommand(line));
    assert(expired(180001, 0, acknowledgementTimeout(line)));
  }
  assert(!homingCommand(nullptr));
  assert(!expired(179999, 0, operationTimeout(true)));
  assert(expired(180000, 0, operationTimeout(true)));
  assert(expired(300000, 0, operationTimeout(false)));
  Barrier barrier; barrier.begin(0); barrier.prepared = 1; assert(!barrier.canStart(true));
  barrier.prepared = 3; assert(!barrier.canStart(false) && barrier.canStart(true));
  barrier.phase = Phase::Running; barrier.done(2); assert(barrier.active());
  barrier.done(1); assert(barrier.phase == Phase::Done);
  barrier.begin(1); barrier.fail(); barrier.done(3); assert(barrier.phase == Phase::Failed);
  std::cout << "Firmware parser, native gate, timeout, and barrier tests passed\n";
}
