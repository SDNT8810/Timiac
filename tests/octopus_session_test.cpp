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

void drivers(OctopusSession& s, int bad = -1, bool stealth = false) {
  s.receive("\t\tX\tY\tZ\tA\tB\tC", now);
  s.receive("Set current\t600\t600\t600\t600\t600\t600", now);
  s.receive("msteps\t\t4\t4\t4\t4\t4\t4", now);
  s.receive(stealth ? "stealthChop\tfalse\tfalse\ttrue\ttrue\ttrue\ttrue" : "stealthChop\tfalse\tfalse\tfalse\tfalse\tfalse\tfalse", now);
  for (unsigned a = 0; a < 6; ++a) {
    std::string line = "Testing "; line += timiac::axes[a];
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
  // A bare ok or old firmware must never authorize movement.
  OctopusSession old; old.next(now); old.receive("ok", now); assert(!old.healthy(now));
  assert(strstr(old.fault, "Wrong Octopus"));
  now += 1001; old.next(now); old.receive("FIRMWARE_NAME:Marlin AXIS_COUNT:6", now); old.receive("ok", now);
  assert(!old.healthy(now));
  // The exact X/Y-vs-Z/A/B/C mode mismatch is rejected before any G28.
  OctopusSession mode; mode.next(now); identify(mode); mode.next(now); drivers(mode, -1, true); mode.receive("ok", now);
  assert(!mode.healthy(now) && mode.drivers.modeOk == 3);
  const char* home = "G28 A"; assert(!mode.stage(&home, 1, now));
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
  assert(strstr(one.stop(now), "M410"));
  one.receive("ok", now); assert(one.state == State::Drain && !one.healthy(now));
  one.receive("TIMIAC_DRAINED", now); one.receive("ok", now); assert(one.state == State::Offline);
  now += 1001; boot(one); assert(one.healthy(now) && one.homed == 0);
  // A homing stop is a real emergency; no automatic firmware-reset or motion retry.
  assert(s.stage(&home, 1, now)); s.next(now); drivers(s); s.receive("ok", now); s.next(now); assert(s.commit());
  s.next(now); s.receive("ok", now); s.next(now); s.receive("ok", now);
  assert(std::string(s.next(now)) == "G28 A");
  assert(std::string(s.stop(now)) == "M112"); assert(s.halted() && s.homed == 0);
  s.receive("ok", now); s.receive("Error:Printer halted. kill() called!", now);
  assert(s.halted() && s.next(now + 200000) == nullptr);
  s.receive("start", now); now += 1001; boot(s); assert(s.healthy(now) && !s.homed);
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
  std::cout << "Octopus kernel: identity, driver modes, all six axes, stop, recovery, homing timeout and switches passed\n";
}
