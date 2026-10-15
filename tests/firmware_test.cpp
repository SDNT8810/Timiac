#include "../shared/gcode.h"
#include "../shared/safety.h"
#include "../shared/peer_outbox.h"
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
  // Wiring supplied by the owner, independently listed in physical board order.
  const unsigned faceHour[12] = {0, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1};
  const float travel = travelMm, halfTravel = travel / 2;
  PeerOutbox outbox;
  const std::string first = std::string(2200, 'a') + '\n', second = std::string(2000, 'b') + '\n';
  assert(outbox.append(first.c_str(), first.size(), 0xfffffff0U));
  assert(!outbox.append(second.c_str(), second.size(), 0xfffffff0U));
  outbox.advance(1500, 0xfffffff0U); assert(outbox.partialFrame);
  assert(outbox.append(second.c_str(), second.size(), 0xfffffff0U)); // Compacts the unsent suffix.
  assert(std::string(outbox.data(), outbox.pending()) == first.substr(1500) + second);
  assert(!outbox.stalled(0xfffffff0U + 14999U, 15000));
  assert(outbox.stalled(0xfffffff0U + 15000U, 15000));
  outbox.advance(first.size() - 1500, 100); assert(!outbox.partialFrame);
  assert(std::string(outbox.data(), outbox.pending()) == second);
  outbox.advance(second.size(), 101); assert(!outbox.pending() && !outbox.partialFrame);
  assert(!outbox.append("unfinished", 10, 0));
  const std::string huge = std::string(PeerOutbox::maxPacket, 'x') + '\n';
  assert(!outbox.append(huge.c_str(), huge.size(), 0));

  assert(nextClockMinute(13 * 60 + 58, true) == 14 * 60);
  assert(nextClockMinute(13 * 60 + 55, true) == 14 * 60);
  assert(nextClockMinute(23 * 60 + 59, true) == 0);
  assert(nextClockMinute(23 * 60 + 35, false) == 35);
  assert(clockHourDue(115, 120) && clockHourDue(119, 120));
  assert(clockHourDue(1435, 0) && clockHourDue(1439, 0));
  assert(clockHourDue(115, 121)); // Delayed by another animation.
  assert(!clockHourDue(-1, 120)); // Startup/resume establishes a pose first.
  assert(!clockHourDue(120, 120) && !clockHourDue(120, 125));
  assert(!clockHourDue(120, 119) && !clockHourDue(115, 240));
  ProximityGate gate;
  for (unsigned now = 0; now < 2000; now += 20) assert(!gate.sample(true, true, now)); // No boot trigger, even unplugged/HIGH.
  assert(!gate.armed);
  assert(!gate.sample(false, true, 2000));
  assert(!gate.sample(false, true, 2499)); assert(!gate.armed);
  assert(!gate.sample(false, true, 2500)); assert(gate.armed);
  assert(!gate.sample(true, true, 2520)); assert(!gate.sample(true, true, 2719));
  assert(gate.sample(true, true, 2720));
  for (unsigned now = 2740; now < 4000; now += 20) assert(!gate.sample(true, true, now)); // Held hand cannot repeat.
  assert(!gate.sample(false, true, 4000)); assert(!gate.sample(false, true, 4500)); assert(gate.armed);
  assert(!gate.sample(true, true, 4520));
  assert(!gate.sample(false, true, 4600)); // A short electrical/optical glitch does not trigger.
  assert(!gate.sample(true, true, 4620)); assert(!gate.sample(true, true, 4819));
  assert(gate.sample(true, true, 4820));
  assert(!gate.sample(false, true, 5000)); assert(!gate.sample(false, true, 5500)); assert(gate.armed);
  assert(!gate.sample(true, true, 5520)); assert(!gate.sample(true, false, 5600)); // Busy/Stop discards pending detection.
  assert(!gate.armed);
  for (unsigned now = 5620; now < 7000; now += 20) assert(!gate.sample(true, true, now));
  gate.reset();
  assert(!gate.sample(false, true, 0xfffffff0U));
  assert(!gate.sample(false, true, 0xfffffff0U + 500U)); assert(gate.armed); // millis wrap.
  assert(!gate.sample(true, true, 0xfffffff0U + 520U));
  assert(gate.sample(true, true, 0xfffffff0U + 720U));
  for (Preset preset : {Preset::Struggle, Preset::Hour, Preset::Curious}) {
    unsigned moves = 0, dwells = 0;
    float positions[12] = {};
    for (unsigned i = 0; i < presetCount(preset); ++i) {
      Step frame;
      assert(presetStep(preset, i, 3 * 60 + 30, travel, timiac::maxFeed, frame));
      for (unsigned b = 0; b < 2; ++b) for (unsigned j = 0; j < frame.count[b]; ++j) {
        const char* line = frame.lines[b][j];
        assert(validNative(line, 6, travel, timiac::maxFeed));
        if (!strncmp(line, "G4 ", 3)) ++dwells;
        if (strncmp(line, "G1 ", 3)) continue;
        ++moves;
        for (unsigned a = 0; a < 6; ++a) {
          const char* word = strchr(line, axes[a]);
          if (word) positions[b * 6 + a] = strtof(word + 1, nullptr);
        }
      }
    }
    assert(moves >= (preset == Preset::Curious ? 20U : preset == Preset::Hour ? 4U : 8U));
    if (preset == Preset::Struggle || preset == Preset::Hour) {
      assert(dwells == 0);
      for (unsigned i = 0; i < 12; ++i) assert(positions[i] == (i == 6 ? 0 : i == 9 ? halfTravel : travel));
    } else {
      assert(dwells == 16);
      for (unsigned i = 0; i < 12; ++i) assert(fabsf(positions[i] - (i == 6 ? travel * .86f : travel * .92f)) < .001f);
    }
    Step invalid; assert(!presetStep(preset, presetCount(preset), 0, travel, timiac::maxFeed, invalid));
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
    assert(userAction("show-time", hour, minute, travel, timiac::maxFeed, pose));
    for (unsigned b = 0; b < 2; ++b) {
      for (unsigned n = 0; n < pose.count[b]; ++n) assert(validNative(pose.lines[b][n], 6, travel, timiac::maxFeed));
      for (unsigned a = 0; a < 6; ++a) {
        const char* axis = strchr(pose.lines[b][1], axes[a]); assert(axis);
        const unsigned index = faceHour[b * 6 + a];
        assert(strtof(axis + 1, nullptr) == (index == minute / 5 ? 0 : index == hour % 12 ? halfTravel : travel));
      }
    }
  }
  // Clock controls advance physically clockwise: 12:00 -> C2 at :05,
  // 12:05 -> B2 at :10; 12:00 -> C2 halfway at 13:00. Midnight wraps.
  for (unsigned base : {0U, 5U, 55U, 12U * 60, 13U * 60 + 35, 23U * 60 + 55}) {
    for (bool five : {true, false}) {
      const unsigned target = nextClockMinute(base, five);
      Step pose; assert(userAction("show-time", target / 60, target % 60, travel, maxFeed, pose));
      for (unsigned arm = 0; arm < 12; ++arm) {
        const char* word = strchr(pose.lines[arm / 6][1], axes[arm % 6]); assert(word);
        assert(strtof(word + 1, nullptr) == (faceHour[arm] == target % 60 / 5 ? 0 : faceHour[arm] == target / 60 % 12 ? halfTravel : travel));
      }
    }
  }
  // The complete hour trajectory has a moving board on every frame. Match
  // nominal durations using Marlin's XYZ-first feed metric; only idle boards wait.
  for (Preset preset : {Preset::Hour, Preset::Struggle}) for (unsigned target : {0U, 60U, 210U, 755U, 1439U}) for (float initial : {0.0f, halfTravel, travel}) {
    float positions[12]; for (float& position : positions) position = initial;
    unsigned commands = 0;
    double bounds[2] = {}; // full stop at every frame: conservative acceleration allowance
    for (unsigned i = 0; i < presetCount(preset); ++i) {
      float previous[12]; memcpy(previous, positions, sizeof(previous));
      Step frame; assert(presetTrajectoryFrame(preset, i, target, travel, maxFeed, positions, frame));
      if (!frame.count[0] && !frame.count[1]) continue;
      ++commands;
      assert(frame.count[0] == 1 && frame.count[1] == 1);
      double seconds[2], largestVelocity = 0; bool moving = false;
      for (unsigned b = 0; b < 2; ++b) {
        const char* line = frame.lines[b][0];
        assert(validTrajectory(line, 6, travel, maxFeed) && trajectoryCommand(line));
        if (!strncmp(line, "G4 P", 4)) seconds[b] = strtod(line + 4, nullptr) / 1000;
        else {
          moving = true; float primary = 0, secondary = 0;
          for (unsigned a = 0; a < 6; ++a) {
            const float delta = positions[b * 6 + a] - previous[b * 6 + a];
            (a < 3 ? primary : secondary) += delta * delta;
          }
          const double path = sqrt(primary > 0 ? primary : secondary);
          const double velocity = strtod(strchr(line, 'F') + 1, nullptr) / 60;
          seconds[b] = path / velocity;
          largestVelocity = fmax(largestVelocity, velocity);
          if (preset == Preset::Struggle) assert(velocity * 60 <= maxFeed * .8 + .001);
          for (unsigned arm = 0; arm < 6; ++arm) {
            const double axisSpeed = fabs(positions[b * 6 + arm] - previous[b * 6 + arm]) / path * velocity * 60;
            assert(axisSpeed <= maxFeed * (preset == Preset::Struggle ? .8 : .7) + .02);
          }
          if (preset == Preset::Struggle && i + 1 < presetCount(preset))
            for (unsigned arm = 0; arm < 12; ++arm)
              assert(fabsf(positions[arm] - travel * (i == 1 ? .8f : .4f)) < .001f);

        }
      }
      assert(moving && fabs(seconds[0] - seconds[1]) < .003);
      const double accel = travelAcceleration * (preset == Preset::Struggle ? .8 : 1.0);
      bounds[0] += seconds[0] * 2 + largestVelocity * .5 / accel;
      bounds[1] += seconds[0] + largestVelocity / accel;
    }
    assert(commands >= (preset == Preset::Struggle ? 3U : 1U) && commands <= (preset == Preset::Struggle ? 4U : 2U));
    assert(bounds[0] + .4 <= (preset == Preset::Struggle ? 56 : 40));
    assert(bounds[1] + .4 <= (preset == Preset::Struggle ? 28 : 20));
    for (unsigned arm = 0; arm < 12; ++arm)
      assert(positions[arm] == (faceHour[arm] == target % 60 / 5 ? 0 : faceHour[arm] == target / 60 % 12 ? halfTravel : travel));
  }
  // Next Hour has two poses, and never closes the minute hand before reopening.
  assert(presetCount(Preset::Hour) == 2);
  for (unsigned target : {0U, 60U, 210U, 1439U}) {
    float positions[12]; for (float& value : positions) value = travel;
    Step frame;
    assert(presetTrajectoryFrame(Preset::Hour, 0, target, travel, maxFeed, positions, frame));
    for (float value : positions) assert(value == 0);
    assert(presetTrajectoryFrame(Preset::Hour, 1, target, travel, maxFeed, positions, frame));
    for (unsigned arm = 0; arm < 12; ++arm)
      assert(positions[arm] == (faceHour[arm] == target % 60 / 5 ? 0 : faceHour[arm] == target / 60 % 12 ? halfTravel : travel));
  }
  // Every arm shares the 30/10/30 mm opening poses, including time arms.
  // Only the final direct-to-time pose separates hour/minute from other arms.
  assert(presetCount(Preset::Struggle) == 4);
  float mixed[12];
  for (unsigned arm = 0; arm < 12; ++arm) mixed[arm] = (arm % 3) * halfTravel;
  for (unsigned target : {0U, 60U, 210U, 1439U}) for (unsigned index = 0; index < 4; ++index) {
    Step frame;
    assert(presetTrajectoryFrame(Preset::Struggle, index, target, travel, maxFeed, mixed, frame));
    for (unsigned arm = 0; arm < 12; ++arm) {
      const float expected = index < 3 ? travel * (index == 1 ? .8f : .4f)
        : faceHour[arm] == target % 60 / 5 ? 0 : faceHour[arm] == target / 60 % 12 ? halfTravel : travel;
      assert(fabsf(mixed[arm] - expected) < .001f);
    }
    for (unsigned b = 0; b < 2; ++b) for (unsigned n = 0; n < frame.count[b]; ++n)
      assert(strncmp(frame.lines[b][n], "G4 ", 3));
  }
  for (const char* action : {"home", "open", "close"}) {
    Step pose; assert(userAction(action, 0, 0, travel, timiac::maxFeed, pose));
    for (unsigned b = 0; b < 2; ++b) for (unsigned n = 0; n < pose.count[b]; ++n)
      assert(validNative(pose.lines[b][n], 6, travel, timiac::maxFeed));
    if (!strcmp(action, "home")) assert(!strcmp(pose.lines[1][0], "G28 X Y Z A B C"));
    else for (unsigned b = 0; b < 2; ++b) for (char axis : std::string(axes))
      assert(strtof(strchr(pose.lines[b][1], axis) + 1, nullptr) == (!strcmp(action, "open") ? 0 : travel));
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
      const char* error = compileLine(line.c_str(), parser, actual, 6, travel, timiac::maxFeed);
      if (error) std::cerr << examples[file] << ": " << line << ": " << error << "\n";
      assert(!error);
      for (unsigned b = 0; b < 2; ++b) for (unsigned i = 0; i < actual.count[b]; ++i)
        assert(validNative(actual.lines[b][i], 6, travel, timiac::maxFeed));
      if (file < 3 && (line.find("G1 ") == 0 || line.find("G4 ") == 0)) {
        Step expected; assert(presetStep(presets[file], frame++, 60, travel, timiac::maxFeed, expected));
        assert(!memcmp(&expected, &actual, sizeof(Step)));
      }
    }
    if (file < 3) assert(frame == presetCount(presets[file]));
  }
  Step rejected;
  assert(!userAction("unknown", 0, 0, travel, timiac::maxFeed, rejected));
  assert(!userAction("show-time", 24, 0, travel, timiac::maxFeed, rejected));
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
    assert(validNative(good, 1, travel, 369.6f));
  for (const char* bad : {"G91", "G28", "G28 Y", "G1 Y1 F180", "G1 X53 F180", "G1 X1 F370",
      "G1 X1 F180 M112", "G1 X1", "M211 S0", "G28 X1", "M112", "G1 X1\nM112", "G1 X1 X2 F180"})
    assert(!validNative(bad, 1, travel, 369.6f));
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
        assert(validNative(step.lines[b][n], 1, travel, 369.6f));
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
  assert(!expired(180000, 0, operationTimeout(true)));
  assert(expired(300000, 0, operationTimeout(true)));
  assert(expired(300000, 0, operationTimeout(false)));
  Barrier barrier; barrier.begin(0); barrier.prepared = 1; assert(!barrier.canStart(true));
  barrier.prepared = 3; assert(!barrier.canStart(false) && barrier.canStart(true));
  barrier.phase = Phase::Running; barrier.done(2); assert(barrier.active());
  barrier.done(1); assert(barrier.phase == Phase::Done);
  barrier.begin(1); barrier.fail(); barrier.done(3); assert(barrier.phase == Phase::Failed);
  // One-board commissioning uses only the boards containing validated lines.
  for (unsigned b = 0; b < 2; ++b) {
    const uint8_t mask = 1U << b, other = mask ^ 3;
    GcodeState manual;
    assert(!compileLine(b ? "G28 Z2 A2 B2 C2" : "G28 Z1 A1 B1 C1", manual, step, 6, travel, timiac::maxFeed));
    assert(commandBoards(step.count[0], step.count[1]) == mask);
    barrier.begin(10, mask);
    assert(barrier.includes(b) && !barrier.includes(1 - b));
    barrier.prepared = other; assert(!barrier.canStart(true));
    barrier.prepared = mask; assert(barrier.canStart(true) && !barrier.canStart(false));
    barrier.phase = Phase::Running; barrier.done(other); assert(barrier.active());
    barrier.done(mask); assert(barrier.phase == Phase::Done);
    barrier.begin(20, mask); barrier.fail(); barrier.done(mask); assert(barrier.phase == Phase::Failed);
  }
  assert(commandBoards(0, 0) == 0 && commandBoards(1, 1) == 3);
  for (uint8_t invalid : {0, 4, 255}) {
    barrier.begin(0, invalid); barrier.prepared = 3;
    assert(barrier.phase == Phase::Failed && !barrier.canStart(true));
  }
  std::cout << "Firmware parser, native gate, timeout, and barrier tests passed\n";
}
