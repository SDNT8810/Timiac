// Exercise the production bridge with a captured serial port and manual clock.
// This covers the UART boundary that the parser-only tests cannot see.
#include "../shared/gcode.h"
#include "../shared/safety.h"
#include "../shared/timeouts.h"
#include "../shared/clock.h"
#include "../shared/endstops.h"
#include <assert.h>
#include <string>
#include <vector>
#include <iostream>
#ifdef NDEBUG
#error "UART regression tests require enabled assertions"
#endif
#define TIMIAC_MASTER 0
#include "../ESP_Timiac/include/config.h"

struct String : std::string {
  using std::string::string;
  String() = default;
  String(char value) : std::string(1, value) {}
  String(const std::string& value) : std::string(value) {}
  bool isEmpty() const { return empty(); }
  int indexOf(const std::string& value) const { return offset(find(value)); }
  int indexOf(char value) const { return offset(find(value)); }
  String substring(size_t begin, size_t end) const { return substr(begin, end - begin); }
  bool startsWith(const char* prefix) const { return compare(0, strlen(prefix), prefix) == 0; }
  void trim() {
    const size_t begin = find_first_not_of(" \t\r\n");
    *this = begin == npos ? "" : substr(begin, find_last_not_of(" \t\r\n") - begin + 1);
  }
  void replace(const char* from, const char* to) {
    size_t at = 0;
    while ((at = find(from, at)) != npos) {
      std::string::replace(at, strlen(from), to);
      at += strlen(to);
    }
  }
 private:
  static int offset(size_t at) { return at == npos ? -1 : int(at); }
};
struct JsonVariant {
  String value;
  template <typename T> T as() const { return T(value); }
};
using JsonArray = std::vector<JsonVariant>;
struct SerialCapture {
  std::string output;
  void print(const String& value) { output += value; }
  void print(char value) { output += value; }
  bool hasOverrun() const { return false; }
  int available() const { return 0; }
  char read() const { assert(false); return 0; }
} Serial;
uint32_t now = 4000;
uint32_t millis() { return now; }
String serialLine;
bool serialDiscard = false;
struct { timiac::Barrier barrier; String error; } operation;
unsigned completions = 0;
unsigned faults = 0, recorded = 0;
bool expectFault = false, systemLatched = false;
String systemFault;
void recordReceipt() { ++recorded; }
void logMessage(const String&) {}
void stopAll(const String& reason);
void finishLocal() { ++completions; }
#include "../ESP_Timiac/src/uart_bridge.h"
void stopAll(const String& reason) {
  if (systemLatched) return;
  if (!expectFault) std::cerr << reason << '\n';
  assert(expectFault);
  ++faults; systemLatched = true; systemFault = reason;
  uart.abort(reason);
}

static void runStep(const timiac::Step& step, unsigned board) {
  assert(step.count[board]);
  JsonArray lines;
  std::string expected = "G21\nG90\n";
  for (unsigned i = 0; i < step.count[board]; ++i) {
    assert(timiac::validNative(step.lines[board][i], 6, 48, 700));
    lines.push_back({step.lines[board][i]});
    expected += std::string(step.lines[board][i]) + '\n';
  }
  expected += "M400\n";
  Serial.output.clear();
  uart.stage(lines);
  assert(uart.prepared());
  assert(Serial.output.empty()); // Preparing must not start a motor.
  const unsigned before = completions;
  uart.commit();
  for (unsigned i = 0; uart.job && i < 12; ++i) {
    ++now;
    uart.tick();
    assert(uart.waiting);
    uart.receive("ok");
  }
  if (Serial.output != expected)
    std::cerr << "Expected UART:\n" << expected << "Actual UART:\n" << Serial.output;
  assert(Serial.output == expected);
  assert(!uart.job && completions == before + 1);
}

int main() {
  assert(kAxisCount == 6 && std::string(kAxes) == "XYZABC");
  uart.tick(); // M115 startup handshake.
  assert(Serial.output == "M115\n");
  uart.receive("FIRMWARE_NAME:Marlin 2.1.2.7");
  uart.receive("ok");
  assert(uart.healthy());
  const unsigned board = TIMIAC_BOARD_ID - 1;
  timiac::GcodeState state;
  timiac::Step step;

  // Home each public axis separately; only that local axis may reach UART.
  for (unsigned a = 0; a < 6; ++a) {
    char command[24], expected[16];
    snprintf(command, sizeof(command), "G28 %c%u", "XYZABC"[a], board + 1);
    assert(!timiac::compileLine(command, state, step, 6, 48, 700));
    snprintf(expected, sizeof(expected), "G28 %c", "XYZABC"[a]);
    assert(step.count[1 - board] == 0 && !strcmp(step.lines[board][0], expected));
    runStep(step, board);
    assert(uart.homed == (1U << (a + 1)) - 1);
  }
  assert(!timiac::compileLine("G90 X1 1 Y1 2 Z1 3 A1 4 B1 5 C1 6 X2 7 Y2 8 Z2 9 A2 10 B2 11 C2 12 F350",
                            state, step, 6, 48, 700));
  assert(!strcmp(step.lines[board][1], board == 0
    ? "G1 X1.000 Y2.000 Z3.000 A4.000 B5.000 C6.000 F350.000"
    : "G1 X7.000 Y8.000 Z9.000 A10.000 B11.000 C12.000 F350.000"));
  runStep(step, board);

  // Release dashboard actions must use the identical names all the way to UART.
  for (const char* action : {"home", "open", "close", "show-time"}) {
    assert(timiac::userAction(action, 3, 30, 48, 700, step));
    runStep(step, board);
  }
  // M114 uses configured G-code letters; ignore the trailing step counters.
  uart.receive("X:1.00 Y:2.00 Z:3.00 A:4.00 B:5.00 C:6.00 Count X:720 Y:1440 Z:2160 A:2880 B:3600 C:4320");
  for (unsigned a = 0; a < 6; ++a) assert(uart.known[a] && uart.positions[a] == a + 1);
  // Idle telemetry is opt-in. The release master leaves the old M115 cadence.
  Serial.output.clear(); now += 600; uart.tick(); assert(Serial.output.empty());
  uart.monitorEndstops(true); uart.tick(); assert(Serial.output == "M119\n");
  assert(!uart.endstops.known);
  uart.receive("Reporting endstop status");
  for (const char* line : {"x_min: open", "y_min: TRIGGERED", "z_min: open",
                           "a_min: TRIGGERED", "b_min: open", "c_min: TRIGGERED"}) uart.receive(line);
  uart.receive("x_max: TRIGGERED"); uart.receive("z_probe: TRIGGERED");
  assert(!uart.endstops.known); // No partial report is published before ok.
  const auto homedBefore = uart.homed;
  const auto completedBefore = completions;
  uart.receive("ok");
  assert(uart.endstops.known == 63 && uart.endstops.triggered == 42);
  assert(uart.endstops.fresh(now));
  assert(uart.homed == homedBefore && completions == completedBefore);
  Serial.output.clear(); now += 249; uart.tick(); assert(Serial.output.empty());
  ++now; uart.tick(); assert(Serial.output == "M119\n");
  uart.receive("x_min: TRIGGERED"); uart.receive("y_min: open");
  uart.receive("a_min: not-a-state"); uart.receive("i_min: TRIGGERED");
  uart.receive("ok");
  assert(uart.endstops.known == 3 && uart.endstops.triggered == 1); // Missing fields become unknown.
  assert(!uart.endstops.fresh(now + timiac::kEndstopFreshMs));

  // A pending poll finishes before prepare; no telemetry enters a homing job.
  now += 250; uart.tick(); assert(uart.waiting && uart.active == "M119");
  JsonArray home = {{"G28 X Y Z A B C"}};
  uart.stage(home); assert(!uart.prepared());
  uart.receive("x_min: open"); uart.receive("ok"); assert(uart.prepared());
  Serial.output.clear(); now += 500; uart.tick(); assert(Serial.output.empty());
  uart.commit(); assert(!uart.endstops.known);
  uart.tick(); uart.receive("ok"); // G21
  uart.tick(); uart.receive("ok"); // G90
  uart.tick(); assert(uart.active == "G28 X Y Z A B C");
  Serial.output.clear(); now += 5000; uart.tick(); assert(Serial.output.empty());
  uart.receive("ok"); uart.tick(); uart.receive("ok"); // M400 completes.
  assert(!uart.job);

  // Every axis can independently trigger and release on both bridge identities.
  const char* names[] = {"x_min", "y_min", "z_min", "a_min", "b_min", "c_min"};
  for (unsigned selected = 0; selected < 6; ++selected) {
    uart.write("M119");
    for (unsigned a = 0; a < 6; ++a) uart.receive(std::string(names[a]) + ": " + (a == selected ? "TRIGGERED" : "open"));
    uart.receive("ok");
    assert(uart.endstops.known == 63 && uart.endstops.triggered == (1U << selected));
  }
  uart.endstops.updatedAt = 0xfffffff0U;
  assert(uart.endstops.fresh(0x10U));
  assert(!uart.endstops.fresh(0x10U + timiac::kEndstopFreshMs));
  uart.invalidate(); assert(!uart.endstops.known);
  uart.ready = uart.healthSeen = true; uart.lastHealth = now;
  uart.monitorEndstops(false); Serial.output.clear(); now += 5001; uart.tick();
  assert(Serial.output == "M115\n"); uart.receive("ok");
  uart.monitorEndstops(true); uart.endstopMonitorAt = now - kPeerTimeout;
  Serial.output.clear(); now += 5001; uart.tick(); assert(Serial.output == "M115\n");
  uart.receive("ok");

  // Replay the reported fault: keep Bad A Endstop, cancel the barrier and
  // ignore late acknowledgements/timeouts until the halted Octopus restarts.
  operation.barrier.begin(now); operation.barrier.phase = timiac::Phase::Running;
  uart.stage(home); uart.commit();
  uart.tick(); uart.receive("ok"); uart.tick(); uart.receive("ok"); uart.tick();
  assert(uart.active == "G28 X Y Z A B C");
  expectFault = true;
  uart.receive("echo:Bad A Endstop?");
  const String rootCause = uart.fault;
  assert(rootCause.startsWith(TIMIAC_BOARD_ID == 1 ? "A1 endstop" : "A2 endstop"));
  assert(uart.halted && !uart.ready && !uart.healthy() && !uart.waiting && !uart.job);
  assert(!operation.barrier.active() && operation.error == rootCause && recorded == 1);
  assert(systemLatched && faults == 1);
  uart.receive("echo:Homing Failed");
  uart.receive("Error:Printer halted. kill() called!");
  uart.abort("Master stop");
  uart.receive("ok"); uart.receive("ok N1 P15 B3");
  assert(uart.fault == rootCause && uart.halted && !uart.ready && faults == 1);
  Serial.output.clear();
  for (unsigned i = 0; i < 10; ++i) { now += 30000; uart.tick(); }
  assert(Serial.output.empty() && uart.fault == rootCause && faults == 1);
  uart.receive("start");
  assert(!uart.halted && !uart.ready && uart.homed == 0);
  now += 1001; uart.tick(); assert(Serial.output == "M115\n");
  uart.receive("FIRMWARE_NAME:Marlin 2.1.2.7"); uart.receive("ok");
  assert(uart.healthy() && uart.fault.isEmpty() && !systemLatched && systemFault.isEmpty());
  assert(operation.barrier.phase == timiac::Phase::Failed && completions == completedBefore + 1);
  // Ordinary Stop must also close the active homing barrier without inventing
  // a hardware fault or issuing any reset/emergency command to Marlin.
  operation.barrier.begin(now); uart.stage(home); uart.commit();
  Serial.output.clear(); uart.abort("Operator stop");
  assert(!operation.barrier.active() && operation.error == "Operator stop");
  assert(!uart.halted && uart.fault.isEmpty() && Serial.output.empty());
  std::cout << "Board " << board + 1 << ": homing fault retained, Stop cancels barrier, reset handshake recovery passed\n";
  std::cout << "Board " << board + 1 << ": switch telemetry, freshness and motion isolation passed\n";
  std::cout << "Board " << board + 1 << ": six-axis UART routing, homing, release actions and M114 passed\n";
}
