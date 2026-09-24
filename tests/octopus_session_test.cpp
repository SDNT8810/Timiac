#ifdef NDEBUG
#error Tests require assertions
#endif
#include <cassert>
#include <iostream>
#include <string>
#include <vector>
#include "../shared/octopus_session.h"
using timiac::OctopusSession;
using State = OctopusSession::State;
uint32_t now = 4000;

void drivers(OctopusSession& s, int bad = -1, bool stealth = false, unsigned microsteps = 32) {
  s.receive("\t\tX\tY\tZ\tI\tJ\tK", now);
  s.receive("Set current\t600\t600\t600\t600\t600\t600", now);
  std::string steps = "msteps\t";
  for (unsigned a = 0; a < 6; ++a) steps += "\t" + std::to_string(microsteps);
  s.receive(steps.c_str(), now);
  s.receive(stealth ? "stealthChop\tfalse\tfalse\ttrue\ttrue\ttrue\ttrue" : "stealthChop\tfalse\tfalse\tfalse\tfalse\tfalse\tfalse", now);
  for (unsigned a = 0; a < 6; ++a) {
    std::string line = "Testing "; line += timiac::DriverReport::labels()[a];
    line += int(a) == bad ? " connection... Error: All HIGH" : " connection... OK";
    s.receive(line.c_str(), now);
  }
}
void identify(OctopusSession& s) {
  const std::string line = std::string("FIRMWARE_NAME:Marlin 2.1.2.7 MACHINE_TYPE:") + timiac::machineId + " EXTRUDER_COUNT:0 AXIS_COUNT:6";
  s.receive(line.c_str(), now); s.receive("ok", now);
}
void boot(OctopusSession& s, int bad = -1) {
  assert(std::string(s.next(now)) == "M115"); identify(s);
  assert(std::string(s.next(now)) == "M122"); drivers(s, bad); s.receive("ok", now);
  assert(s.state == State::Ready);
}
std::string drainMarker(const char* command) {
  assert(command);
  const char* marker = strstr(command, "M118 ");
  assert(marker); return marker + 5;
}
void drain(OctopusSession& s) {
  const std::string marker = drainMarker(s.stop(now));
  s.receive(marker.c_str(), now);
  assert(s.state == State::Drain); // The marker's own ok must also be consumed.
  s.receive("ok", now);
  assert(s.state == State::Offline);
}
std::vector<std::string> execute(OctopusSession& s, const char* line) {
  assert(s.stage(&line, 1, now));
  const char* prep = s.next(++now);
  if (prep) { assert(std::string(prep) == "M122"); drivers(s); s.receive("ok", now); s.next(++now); }
  assert(s.prepared()); assert(s.commit());
  std::vector<std::string> sent;
  for (unsigned i = 0; i < 50 && s.running(); ++i) {
    const char* command = s.next(++now);
    if (command) {
      sent.push_back(command);
      s.receive("ok", now);
    }
  }
  assert(!s.running()); return sent;
}
int main() {
  OctopusSession s; boot(s); assert(s.healthy(now));
  for (unsigned a = 0; a < 6; ++a) {
    std::string command = "G28 "; command += timiac::axes[a];
    auto sent = execute(s, command.c_str());
    assert((sent == std::vector<std::string>{"G21", "G90", command, "M400", "M114"}));
    assert(s.homed == (1U << (a + 1)) - 1);
  }
  auto sent = execute(s, "G28 X Y Z A B C");
  assert((sent == std::vector<std::string>{"G21", "G90", "G28 X", "G28 Y", "G28 Z", "G28 A", "G28 B", "G28 C", "M400", "M114"}));
  assert(s.homed == 63);
  execute(s, "G1 X1 Y2 Z3 A4 B5 C6 F350");
  // Route all twelve PUBLIC addresses through the actual parser and kernel.
  for (unsigned board = 0; board < 2; ++board) {
    OctopusSession local; boot(local);
    for (unsigned axis = 0; axis < 6; ++axis) {
      char input[20]; snprintf(input, sizeof(input), "G28 %c%u", timiac::axes[axis], board + 1);
      timiac::Step step; timiac::GcodeState parser;
      assert(!timiac::compileLine(input, parser, step, 6, 48, 700));
      assert(step.count[board] == 1 && step.count[1 - board] == 0);
      auto commands = execute(local, step.lines[board][0]);
      assert(commands[2] == std::string("G28 ") + timiac::axes[axis]);
    }
  }
  // Preparing checks publish atomically; idle switches never interrupt a job.
  const char* allHome = "G28 X Y Z A B C";
  assert(s.stage(&allHome, 1, now));
  assert(std::string(s.next(now)) == "M122");
  assert(s.drivers.good() && !s.prepared());
  drivers(s); s.receive("ok", now); s.next(now); assert(s.prepared());
  assert(!s.next(now + 501)); // Still waiting for both-board commit.
  drain(s); now += 1001; boot(s);
  // A bare ok or old firmware must never authorize movement.
  OctopusSession old; old.next(now); old.receive("ok", now); assert(!old.healthy(now));
  assert(strstr(old.fault, "Wrong Octopus"));
  now += 1001; old.next(now); old.receive("FIRMWARE_NAME:Marlin AXIS_COUNT:6", now); old.receive("ok", now);
  assert(!old.healthy(now));
  // The exact X/Y-vs-Z/A/B/C mode mismatch is rejected before any G28.
  OctopusSession mode; mode.next(now); identify(mode); mode.next(now); drivers(mode, -1, true); mode.receive("ok", now);
  assert(!mode.healthy(now) && mode.drivers.modeOk == 3);
  const char* home = "G28 A"; assert(!mode.stage(&home, 1, now));
  // A driver left at the previous four-microstep setting must block motion.
  OctopusSession wrongSteps; wrongSteps.next(now); identify(wrongSteps); wrongSteps.next(now);
  drivers(wrongSteps, -1, false, 4); wrongSteps.receive("ok", now);
  assert(!wrongSteps.healthy(now) && wrongSteps.drivers.stepsOk == 0);
  assert(!wrongSteps.stage(&home, 1, now));
  // A truncated or reordered report cannot borrow values from the last report.
  OctopusSession partial; boot(partial); assert(partial.stage(&home, 1, now));
  partial.next(now); partial.receive("ok", now);
  assert(!partial.healthy(now) && !partial.prepared());
  for (int a = 0; a < 6; ++a) {
    OctopusSession broken; boot(broken, a);
    assert(!broken.healthy(now)); assert(broken.drivers.connected == (63U & ~(1U << a)));
    assert(!broken.stage(&home, 1, now));
  }
  // After an individual home the kernel allows only that axis' move.
  OctopusSession one; boot(one); execute(one, "G28 A");
  execute(one, "G1 A1 F180");
  const char* badMove = "G1 B1 F180"; assert(!one.stage(&badMove, 1, now));
  // Stop really reaches Marlin and drains late acknowledgements before reconnecting.
  const char* move = "G1 A2 F180"; assert(one.stage(&move, 1, now)); one.next(now); assert(one.commit());
  const char* stopping = one.stop(now); assert(strstr(stopping, "M410"));
  const std::string stoppedMarker = drainMarker(stopping);
  one.receive("ok", now); assert(one.state == State::Drain && !one.healthy(now));
  one.receive(stoppedMarker.c_str(), now); assert(one.state == State::Drain);
  one.receive("ok", now); assert(one.state == State::Offline);
  now += 1001; boot(one); assert(one.healthy(now) && one.homed == 0);
  // Reproduce the screenshot: an idle timeout and missing Stop reply must not
  // invent a Marlin halt. Recover only after the latest drain marker AND ok.
  OctopusSession disconnected; boot(disconnected); execute(disconnected, "G28 Z");
  disconnected.monitor = true; now += 501;
  assert(std::string(disconnected.next(now)) == "M119");
  now += 10001; assert(!disconnected.next(now));
  assert(strstr(disconnected.fault, "M119") && disconnected.failures == 1);
  std::string previous = drainMarker(disconnected.stop(now));
  for (unsigned retry = 0; retry < 3; ++retry) {
    assert(!disconnected.next(now + 9999));
    now += 10000;
    std::string current = drainMarker(disconnected.next(now));
    assert(current != previous && !disconnected.halted() && !disconnected.healthy(now));
    assert(disconnected.state == State::Drain && disconnected.failures == 1);
    disconnected.receive(previous.c_str(), now); disconnected.receive("ok", now);
    assert(disconnected.state == State::Drain); // A superseded attempt cannot authorize a handshake.
    assert(!disconnected.stage(&home, 1, now));
    previous = current;
  }
  disconnected.receive(("echo:" + previous).c_str(), now);
  assert(disconnected.state == State::Drain && !disconnected.next(now));
  disconnected.receive("ok", now); assert(disconnected.state == State::Offline);
  now += 1001; boot(disconnected);
  assert(disconnected.healthy(now) && !disconnected.homed && disconnected.completions == 1);
  assert(!disconnected.next(now)); // Cancelled motion is never replayed.
  // A timeout is still a new failure when an earlier driver fault is retained.
  OctopusSession faulty; boot(faulty, 2); now += 5001;
  assert(std::string(faulty.next(now)) == "M122");
  now += 10001; faulty.next(now);
  assert(faulty.failures == 1 && strstr(faulty.fault, "Driver check failed"));
  drain(faulty); now += 1001; boot(faulty);
  assert(faulty.healthy(now));
  // A homing stop is a real emergency; no automatic firmware-reset or motion retry.
  s.receive("start", now); now += 1001; boot(s);
  assert(s.stage(&home, 1, now)); s.next(now); drivers(s); s.receive("ok", now); s.next(now); assert(s.commit());
  s.next(now); s.receive("ok", now); s.next(now); s.receive("ok", now);
  assert(std::string(s.next(now)) == "G28 A");
  assert(std::string(s.stop(now)) == "M112"); assert(s.halted() && s.homed == 0);
  s.receive("ok", now); s.receive("Error:Printer halted. kill() called!", now);
  assert(s.halted() && s.next(now + 200000) == nullptr);
  s.fail("Octopus serial receive overrun");
  assert(s.halted()); // A later communication error cannot unlock a killed board.
  s.receive("start", now); now += 1001; boot(s); assert(s.healthy(now) && !s.homed);
  // Preserve the first fault, including the exact failed axis, across Stop.
  OctopusSession badSwitch; boot(badSwitch);
  badSwitch.receive("echo:Bad A Endstop?", now);
  badSwitch.receive("Error:Printer halted. kill() called!", now); badSwitch.stop(now);
  assert(std::string(badSwitch.fault) == "echo:Bad A Endstop?" && badSwitch.halted());
  // Homing can run past the old 60/120-second deadlines. Timeout still stops it.
  assert(s.stage(&home, 1, now)); s.next(now); drivers(s); s.receive("ok", now); s.next(now); s.commit();
  s.next(now); s.receive("ok", now); s.next(now); s.receive("ok", now); s.next(now);
  assert(s.next(now + 120001) == nullptr && s.running());
  assert(s.next(now + 180001) == nullptr && !s.running());
  assert(std::string(s.stop(now + 180001)) == "M112");
  // Diagnostics are atomic, and can run even if a driver's communication fails.
  OctopusSession lamps; boot(lamps, 2); lamps.monitor = true; now += 501;
  assert(std::string(lamps.next(now)) == "M119");
  for (const char* line : {"x_min: open", "y_min: open", "z_min: TRIGGERED", "a_min: open", "b_min: open", "c_min: open"}) lamps.receive(line, now);
  assert(!lamps.endstops.known); lamps.receive("ok", now);
  assert(lamps.endstops.known == 63 && lamps.endstops.triggered == 4);
  // Passing drivers are rechecked automatically, without a motion job or the
  // other board. A completed replacement report immediately changes the result.
  OctopusSession live; boot(live); live.monitor = true;
  assert(live.driverReportAvailable && live.driverReportAge(now) == 0);
  now += 2001; assert(std::string(live.next(now)) == "M122");
  drivers(live, 2);
  assert(live.drivers.good()); // A partial report cannot overwrite completed checks.
  live.receive("ok", now);
  assert(!live.healthy(now) && live.drivers.connected == 59 && live.driverReportAge(now) == 0);
  now += 2001; assert(std::string(live.next(now)) == "M122");
  drivers(live); live.receive("ok", now);
  assert(live.healthy(now) && live.drivers.connected == 63 && !live.homed);
  now += 1200; assert(live.driverReportAge(now) == 1200);
  live.receive("start", now);
  assert(!live.driverReportAvailable && !live.drivers.tested);
  std::cout << "Octopus kernel: identity, driver modes, all six axes, stop, recovery, homing timeout and switches passed\n";
}
