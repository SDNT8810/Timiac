#define TIMIAC_MASTER 1
#ifndef TIMIAC_RELEASE
#define TIMIAC_RELEASE 0
#endif
#define TIMIAC_BOARD_ID 0
#define ARDUINOJSON_ENABLE_ARDUINO_STRING 1
#include "host/Arduino.h"
#include <ArduinoJson.h>
#include <cassert>
#include <iostream>
#include "../ESP_Timiac/include/config.h"
#include "../shared/safety.h"
#include "../shared/peer_outbox.h"
#include "../shared/timeouts.h"
#include "../shared/gcode.h"
#include "../shared/endstops.h"
#include "../shared/driver_report.h"
#include "../shared/presets.h"
#include "../shared/proximity.h"
#include "../shared/network_memory.h"
static uint32_t networkFreeHeap = 128U * 1024, networkLargestBlock = 64U * 1024;
inline bool platformNetworkMemoryReady(bool secure) {
  return timiac::networkMemoryReady(networkFreeHeap, networkLargestBlock, secure);
}
inline uint32_t platformFreeNetworkHeap() { return networkFreeHeap; }
inline uint32_t platformLargestNetworkBlock() { return networkLargestBlock; }
WiFiServer listener;
#include "../ESP_Timiac/src/runtime.h"
struct Preferences {
  unsigned saved = timiac::defaultSpeedPercent;
  bool writable = true;
  String savedString;
  bool begin(const char*, bool) { return true; }
  unsigned getUInt(const char*, unsigned) { return saved; }
  size_t putUInt(const char*, unsigned value) { if (!writable) return 0; saved = value; return sizeof(value); }
  String getString(const char*, const char* fallback) { return savedString.isEmpty() ? String(fallback) : savedString; }
  size_t putString(const char*, const String& value) { if (!writable) return 0; savedString = value; return value.length(); }
};
#include "../ESP_Timiac/src/motion_speed.h"
#include "../ESP_Timiac/src/uart_bridge.h"
#include "../ESP_Timiac/src/peer_link.h"
#include "../ESP_Timiac/src/builtin_program.h"
#include "../ESP_Timiac/src/program_runner.h"
bool rtcValid = true;
String rtcText = "2026-10-20T13:45:00";
struct TestRtc {
  unsigned h = 13, m = 45;
  unsigned hour() const { return h; }
  unsigned minute() const { return m; }
} rtcTime;
#include "../ESP_Timiac/src/release_clock.h"
constexpr unsigned INPUT = 0;
unsigned photoelectricLevel = 0; // Zero ADC counts; calibrated API alone returns 75 mV.
bool photoelectricAdcValid = true;
void pinMode(unsigned pin, unsigned mode) { assert(pin == 34 && mode == INPUT); }
bool configureProximityAdc(unsigned pin) { assert(pin == 34); return true; }
bool readProximityAdc(uint16_t& raw, uint32_t& mv, uint32_t& floor) {
  raw = photoelectricLevel * 4; mv = photoelectricLevel ? photoelectricLevel : 75; floor = 75;
  return photoelectricAdcValid;
}
#include "../ESP_Timiac/src/proximity_sensor.h"

void resetMaster() {
  hostNow = 1000; bootId = "master-boot"; stopToken = stopReason = systemFault = "";
  systemLatched = stopPending = stopRequested = networkChanging = false; stopRequired = stopRecoveryRequired = 0;
  operation = Operation(); program = Program(); receiptNext = 0;
  releaseClockEnabled = false; armTimeMinutes = releaseClockMinute = -1;
  lastStopReason = lastStopSession = clockPauseReason = ""; lastStopAt = lastStopCount = 0;
  releaseStartupPending = TIMIAC_MASTER && TIMIAC_RELEASE;
  motionSpeedPercent = timiac::defaultSpeedPercent;
  rtcValid = true; rtcTime.h = 13; rtcTime.m = 45;
  lastStruggleVariant = timiac::struggleVariantCount;
  configureProximity(); proximityTriggers = 0; photoelectricLevel = 0; photoelectricAdcValid = true;
  firmwareUpdating = firmwareUpdateCancel = false;
  cloudInFlight = false; networkFreeHeap = 128U * 1024; networkLargestBlock = 64U * 1024;
  WiFi.state = WL_CONNECTED;
  for (auto& r : receipts) r = Receipt();
  for (unsigned b = 0; b < 2; ++b) { peers[b] = Peer(); boards[b] = RemoteBoard(); lastBoardBoot[b] = ""; }
}
void connectBoard(unsigned b) {
  auto& p = peers[b]; p.board = b; p.paired = true; p.boot = "bridge-" + String(b);
  p.socket.connectedFlag = true; p.rx = p.tx = hostNow;
  lastBoardBoot[b] = p.boot;
  auto& board = boards[b]; board.online = true; board.stateAt = hostNow;
  board.drivers.tested = board.drivers.connected = board.drivers.currentOk = board.drivers.stepsOk = board.drivers.modeOk = 63;
  board.drivers.columns = true; board.drivers.settings = 7;
}
void report(unsigned b, const char* phase, bool prepared = false, const char* fault = "", uint32_t reset = 0) {
  DynamicJsonDocument packet(4096);
  packet["type"] = "state"; packet["from"] = peers[b].boot; packet["session"] = bootId;
  packet["id"] = operation.id; packet["phase"] = phase; packet["prepared"] = prepared;
  packet["latched"] = false; packet["stopAck"] = boards[b].stopAck;
  auto board = packet.createNestedObject("board");
  writeBoard(board, b); board["fault"] = fault; board["reset"] = reset;
  String text; serializeJson(packet, text); processPeer(peers[b], text);
}
String lastType(unsigned b) {
  assert(!peers[b].socket.sent.empty());
  DynamicJsonDocument packet(4096); assert(!deserializeJson(packet, peers[b].socket.sent.back()));
  return packet["type"].as<String>();
}
void startHome(unsigned b, bool coordinated = false) {
  DynamicJsonDocument doc(2048); timiac::GcodeState parser;
  assert(!translate(b ? "G28 Z2 A2 B2 C2" : "G28 Z1 A1 B1 C1", parser, doc.to<JsonObject>()));
  if (coordinated) startOperation(doc["a"], doc["b"], "home", 100);
  else startOperation(doc["a"], doc["b"], "home", 100, timiac::commandBoards(doc["a"].size(), doc["b"].size()));
}
#if TIMIAC_RELEASE
// Run the production scheduler, trajectory builder and UART queue against two
// simulated motor boards. Marlin's inactivity timer is refreshed by actual
// planner motion, not M122/M114 or a G1 with unchanged coordinates.
unsigned clockSoak(bool honorHoldSetup, bool parkHome = false) {
  resetMaster(); connectBoard(0); connectBoard(1);
  releaseStartupPending = false; releaseClockEnabled = true;
  struct Motor {
    timiac::OctopusSession uart;
    float position[6] = {};
    uint32_t lastMove = 1000, idleTimeout = 600000;
    bool trusted = true;
  } motors[2];
  for (unsigned b = 0; b < 2; ++b) {
    boards[b].homed = 63; boards[b].trajectorySupported = true;
    for (unsigned a = 0; a < 6; ++a) {
      boards[b].known[a] = true;
      motors[b].position[a] = boards[b].positions[a] = kTravelMm;
    }
    motors[b].uart.identity = true;
    motors[b].uart.state = timiac::OctopusSession::State::Ready;
    motors[b].uart.homed = 63; motors[b].uart.drivers = boards[b].drivers;
  }
  unsigned hourly = 0, poses = 0;
  for (unsigned minute = 0; minute <= 1440; ++minute) {
    hostNow = 1000 + minute * 60000U;
    rtcTime.h = minute / 60 % 24; rtcTime.m = minute % 60;
    for (unsigned b = 0; b < 2; ++b) {
      peers[b].rx = boards[b].stateAt = hostNow;
      peers[b].socket.writeCapacity = 65536;
      if (motors[b].idleTimeout && hostNow - motors[b].lastMove >= motors[b].idleTimeout)
        motors[b].trusted = false;
    }
    if (parkHome && minute == 1) {
      DynamicJsonDocument home(2048); timiac::Step step;
      timiac::userAction("home", 0, 0, kTravelMm, kMaxFeed, step);
      assert(writePresetStep(step, home.to<JsonVariant>()));
      startOperation(home["a"], home["b"], "park-home", 42);
      trackManualClockAction("home");
    }
    releaseClockTick();
    if (program.state == "running") { ++hourly; programTick(); }
    if (operation.barrier.active()) {
      ++poses;
      for (unsigned b = 0; b < 2; ++b) {
        auto& motor = motors[b];
        while (peers[b].outbox.pending()) assert(flushPeer(peers[b]));
        DynamicJsonDocument packet(4096);
        assert(!deserializeJson(packet, peers[b].socket.sent.back()));
        const char* lines[timiac::maxTrajectoryCommands]; unsigned count = 0;
        for (JsonVariant line : packet["lines"].as<JsonArray>()) lines[count++] = line.as<const char*>();
        motor.uart.lastRx = hostNow;
        assert(motor.uart.stage(lines, count, hostNow, packet["trajectory"] | false, packet["speedPercent"] | 100U));
        const char* check = motor.uart.next(hostNow);
        if (check) { // Home preparation requests the already simulated driver report.
          assert(!strcmp(check, "M122"));
          motor.uart.waiting = false; motor.uart.next(hostNow);
        }
        assert(motor.uart.prepared()); report(b, "preparing", true);
      }
      operationTick(); assert(operation.barrier.phase == timiac::Phase::Running);
      for (unsigned b = 0; b < 2; ++b) {
        auto& motor = motors[b]; assert(motor.uart.commit());
        while (motor.uart.running()) if (const char* line = motor.uart.next(hostNow)) {
          if (!strcmp(line, "M84 S0") && honorHoldSetup) motor.idleTimeout = 0;
          if (timiac::homingCommand(line)) {
            motor.trusted = true; motor.lastMove = hostNow;
            for (float& p : motor.position) p = 0;
          }
          if (!strncmp(line, "G1 ", 3)) {
            if (!motor.trusted) return minute; // Marlin: Home XYZABC First.
            for (unsigned a = 0; a < 6; ++a) if (const char* word = strchr(line, kAxes[a])) {
              float target = strtof(word + 1, nullptr);
              if (fabsf(target - motor.position[a]) > .0001f) motor.lastMove = hostNow;
              motor.position[a] = target;
            }
          }
          motor.uart.receive("ok", hostNow);
        }
        for (unsigned a = 0; a < 6; ++a) boards[b].positions[a] = motor.position[a];
        report(b, "done");
      }
      programTick();
    }
    assert(!systemLatched && !operation.barrier.active() && !program.reserved());
    assert(releaseClockEnabled == !(parkHome && minute >= 1));
    for (auto& peer : peers) { peer.socket.sent.clear(); peer.socket.wire.clear(); }
  }
  assert(parkHome ? poses == 2 && hourly == 0 : hourly == 24 && poses == 289);
  for (const auto& motor : motors) assert(motor.trusted);
  return 0;
}
#endif
int main() {
#if TIMIAC_RELEASE
  assert(clockSoak(false) == 15); // Reproduce the old idle-board shutdown.
  assert(clockSoak(true) == 0);   // An entire day, no browser lease/manual wake-up.
  assert(clockSoak(true, true) == 0); // Manual Home holds for the full day too.
#endif
  resetMaster(); configureMotionSpeed(); assert(motionSpeedPercent == 75);
  DynamicJsonDocument speedSetting(256);
  for (const char* invalid : {"{}", "{\"percent\":19}", "{\"percent\":20}", "{\"percent\":50}", "{\"percent\":100}", "{\"percent\":101}", "{\"percent\":-1}", "{\"percent\":75.5}", "{\"percent\":\"75\"}", "{\"percent\":true}"}) {
    deserializeJson(speedSetting, invalid);
    assert(saveMotionSpeed(speedSetting.as<JsonVariant>()) && motionSpeedPercent == 75);
  }
  speedSetting.clear(); speedSetting["percent"] = 75;
  assert(!saveMotionSpeed(speedSetting.as<JsonVariant>()) && motionSpeedPercent == 75);
  motionSpeedPercent = 20; configureMotionSpeed(); assert(motionSpeedPercent == 75);
  // Every entry point captures the same speed for both bridges. Changing a
  // later setting cannot alter an already prepared operation or its timeout.
  for (unsigned speed : {20U, 50U, 100U}) {
    resetMaster(); connectBoard(0); connectBoard(1); motionSpeedPercent = speed;
    startHome(0, true);
    for (unsigned b = 0; b < 2; ++b) {
      DynamicJsonDocument packet(4096);
      assert(!deserializeJson(packet, peers[b].socket.sent.back()));
      assert(packet["speedPercent"] == speed);
    }
    motionSpeedPercent = 100;
    assert(operation.speedPercent == speed);
    assert(timiac::operationTimeout(true, operation.speedPercent) == 30000000U / speed);
  }
#if TIMIAC_RELEASE
  // Manual Home takes over from running automatic tracking and holds across
  // five-minute/hour boundaries. Start clock explicitly resumes tracking.
  resetMaster(); connectBoard(0); connectBoard(1);
  releaseClockEnabled = true; releaseClockMinute = 13 * 60 + 59;
  startHome(0, true); trackManualClockAction("home");
  report(0, "preparing", true); report(1, "preparing", true); operationTick();
  boards[0].homed = boards[1].homed = 63;
  report(0, "done"); report(1, "done");
  for (unsigned minute : {0U, 5U, 10U, 55U}) {
    rtcTime.h = 14; rtcTime.m = minute; releaseClockTick();
    assert(operation.id == "home" && !operation.barrier.active() && !program.reserved());
    assert(!releaseClockEnabled && !releaseStartupPending);
  }
  trackManualClockAction("show-time"); releaseClockTick();
  assert(releaseClockEnabled && operation.barrier.active() && !operation.homing);
  // The new Open control parks with a normal move, and never resumes the clock
  // when an hour or five-minute boundary passes.
  resetMaster(); connectBoard(0); connectBoard(1);
  boards[0].homed = boards[1].homed = 63;
  releaseClockEnabled = true;
  timiac::Step openStep; assert(timiac::userAction("open", 0, 0, kTravelMm, kMaxFeed, openStep));
  DynamicJsonDocument openCommand(2048);
  assert(writePresetStep(openStep, openCommand.to<JsonVariant>()));
  startOperation(openCommand["a"], openCommand["b"], "open", 101);
  trackManualClockAction("open");
  assert(operation.barrier.active() && !operation.homing);
  report(0, "preparing", true); report(1, "preparing", true); operationTick();
  report(0, "done"); report(1, "done");
  for (unsigned minute : {0U, 5U, 55U}) {
    rtcTime.h = 15; rtcTime.m = minute; releaseClockTick();
    assert(operation.id == "open" && !operation.barrier.active() && !program.reserved());
    assert(!releaseClockEnabled && !releaseStartupPending);
  }
#endif
  // A received current must survive bridge -> master -> browser unchanged,
  // including a mismatch. Do not replace actual reports with expectations.
  resetMaster(); connectBoard(0);
  for (unsigned i = 0; i < 6; ++i) {
    boards[0].drivers.currentMa[i] = 600; boards[0].drivers.microsteps[i] = 32;
  }
  boards[0].drivers.currentOk = boards[0].drivers.stepsOk = 0;
  report(0, "idle");
  DynamicJsonDocument settings(4096); writeBoard(settings.to<JsonObject>(), 0);
  for (unsigned i = 0; i < 6; ++i) {
    assert(settings["drivers"]["currentMa"][i] == 600);
    assert(settings["drivers"]["microstepValues"][i] == 32);
  }
  assert(!boards[0].drivers.good());
  // Release boots without a phone: wait for both healthy, idle boards, then
  // prepare one full six-arm home on each. RTC validity never delays homing.
  resetMaster(); rtcValid = false;
  releaseClockTick(); assert(!operation.barrier.active());
  connectBoard(0); releaseClockTick(); assert(!operation.barrier.active());
  connectBoard(1); boards[1].drivers.currentOk = 0;
  releaseClockTick(); assert(!operation.barrier.active());
  boards[1].drivers.currentOk = 63; boards[1].busy = true;
  releaseClockTick(); assert(!operation.barrier.active());
  boards[1].busy = false; program.state = "running";
  releaseClockTick(); assert(!operation.barrier.active());
  program = Program();
  // Even a master-only restart must home again, despite old homed reports.
  boards[0].homed = boards[1].homed = 63;
  releaseClockTick();
#if TIMIAC_RELEASE
  assert(operation.homing && operation.barrier.active() && operation.barrier.required == 3);
  assert(!releaseStartupPending && releaseClockEnabled && armTimeMinutes == -1 && !lease.live());
  const String startupId = operation.id;
  for (unsigned b = 0; b < 2; ++b) {
    DynamicJsonDocument packet(4096);
    assert(!deserializeJson(packet, peers[b].socket.sent.back()));
    assert(packet["type"] == "prepare" && packet["homing"] == true);
    assert(packet["lines"].size() == 1 && packet["lines"][0] == "G28 X Y Z A B C");
  }
  report(0, "preparing", true); operationTick();
  assert(lastType(0) == "prepare");
  report(1, "preparing", true); operationTick();
  assert(lastType(0) == "commit" && lastType(1) == "commit");
  rtcValid = true; rtcTime.m = 51;
  releaseClockTick(); assert(operation.id == startupId);
  report(0, "done"); releaseClockTick(); assert(operation.id == startupId);
  boards[1].homed = 0; report(1, "done");
  assert(operation.barrier.phase == timiac::Phase::Done);
  releaseClockTick(); assert(operation.id == startupId); // All twelve must be homed.
  boards[1].homed = 63; report(1, "done");
  rtcValid = false; releaseClockTick(); assert(operation.id == startupId);
  rtcValid = true; rtcTime.m = 55; releaseClockTick();
  assert(operation.id != startupId && !operation.homing && armTimeMinutes == 835);
  for (unsigned b = 0; b < 2; ++b) {
    DynamicJsonDocument packet(4096);
    assert(!deserializeJson(packet, peers[b].socket.sent.back()));
    timiac::Step expected;
    assert(timiac::userAction("show-time", 13, 55, kTravelMm, kMaxFeed, expected));
    assert(packet["lines"].size() == expected.count[b]);
    for (unsigned i = 0; i < expected.count[b]; ++i) assert(packet["lines"][i] == expected.lines[b][i]);
  }
  report(0, "preparing", true); report(1, "preparing", true); operationTick();
  report(0, "done"); report(1, "done");
  const String initialTimeId = operation.id;
  releaseClockTick(); assert(operation.id == initialTimeId);
  peers[1].socket.stop(); networkTick(); connectBoard(1);
  releaseClockTick(); assert(operation.id == initialTimeId && !releaseStartupPending);

  // Stop while waiting for boards cancels startup even after Stop recovery.
  resetMaster(); stopAll("Operator stop before startup"); recoverStop();
  assert(!releaseStartupPending && !releaseClockEnabled && !systemLatched);
  connectBoard(0); connectBoard(1);
  for (unsigned b = 0; b < 2; ++b) { boards[b].stopAck = stopToken; report(b, "idle"); }
  releaseClockTick(); assert(!operation.barrier.active() && bothHealthy());

  // An actual failed home never retries or advances to the clock after recovery.
  resetMaster(); connectBoard(0); connectBoard(1); releaseClockTick();
  report(0, "preparing", true); report(1, "preparing", true); operationTick();
  report(1, "failed", false, "Six-arm homing: endstop did not release [axes: C]");
  assert(operation.barrier.phase == timiac::Phase::Failed && systemLatched);
  for (unsigned b = 0; b < 2; ++b) {
    boards[b].homed = 63; boards[b].stopAck = stopToken; report(b, "idle");
  }
  assert(bothHealthy() && !releaseStartupPending && !releaseClockEnabled);
  releaseClockTick(); assert(operation.barrier.phase == timiac::Phase::Failed);

  // A manual command accepted first must not leave a surprise startup queued.
  resetMaster(); connectBoard(0); connectBoard(1); startHome(0);
  report(0, "preparing", true); operationTick(); report(0, "done");
  releaseClockTick(); assert(operation.id == "home" && !operation.barrier.active() && !releaseStartupPending);
#else
  assert(!operation.barrier.active() && !releaseClockEnabled && !releaseStartupPending);
  assert(peers[0].socket.sent.empty() && peers[1].socket.sent.empty());
#endif
  // Run the actual master clock loop in both build modes, without a browser.
  resetMaster(); connectBoard(0); connectBoard(1);
  releaseStartupPending = false; // Startup was cancelled; explicit resume required.
  boards[0].homed = boards[1].homed = 63;
  releaseClockTick(); assert(!operation.barrier.active()); // Explicit start required.
  releaseClockEnabled = true;
  rtcValid = false; releaseClockTick(); assert(!operation.barrier.active());
  rtcValid = true; boards[1].homed = 0; releaseClockTick(); assert(!operation.barrier.active());
  boards[1].homed = 63;
  program.state = "running"; releaseClockTick(); assert(!operation.barrier.active());
  program = Program();
  boards[1].online = false; releaseClockTick(); assert(!operation.barrier.active());
  boards[1].online = true;
  releaseClockTick();
#if TIMIAC_RELEASE
  assert(operation.barrier.active() && operation.barrier.required == 3);
  assert(armTimeMinutes == 825 && !lease.live());
  const String clockId = operation.id;
  releaseClockTick(); assert(operation.id == clockId); // Never overlap a move.
  operation = Operation();
  rtcTime.m = 49; releaseClockTick(); assert(!operation.barrier.active());
  rtcTime.m = 50; releaseClockTick(); assert(operation.barrier.active() && armTimeMinutes == 830);
  operation = Operation(); armTimeMinutes = -1; // Gesture completed; restore RTC pose.
  releaseClockTick(); assert(operation.barrier.active());
  stopAll("Operator stop"); assert(!releaseClockEnabled);
  assert(!timiac::clockPoseDue(1435, 1439));
  assert(timiac::clockPoseDue(1435, 0)); // Midnight and RTC changes.
  assert(timiac::clockPoseDue(60, 55));
#else
  assert(!operation.barrier.active() && armTimeMinutes == -1);
#endif
  // Every automatic hour boundary, including midnight, runs the same dance
  // as Next hour but targets the current RTC hour, not one hour beyond it.
#if TIMIAC_RELEASE
  for (unsigned hour = 0; hour < 24; ++hour) {
    resetMaster(); connectBoard(0); connectBoard(1);
    releaseStartupPending = false; releaseClockEnabled = true;
    for (auto& board : boards) {
      board.homed = 63; board.trajectorySupported = true;
      for (unsigned a = 0; a < 6; ++a) { board.known[a] = true; board.positions[a] = kTravelMm; }
    }
    rtcTime.h = (hour + 23) % 24; rtcTime.m = 59;
    armTimeMinutes = rtcTime.h * 60 + 55;
    // Start from the actual :55 clock pose, including an open minute arm and
    // a half-open hour arm, rather than the unrelated all-closed pose.
    timiac::Step previousPose;
    assert(timiac::userAction("show-time", rtcTime.h, 55, kTravelMm, kMaxFeed, previousPose));
    for (unsigned b = 0; b < 2; ++b) for (unsigned a = 0; a < 6; ++a)
      boards[b].positions[a] = strtof(strchr(previousPose.lines[b][1], kAxes[a]) + 1, nullptr);
    releaseClockMinute = hour % 2 ? armTimeMinutes : -1; // Also cover manual Start clock at :59.
    releaseClockTick(); assert(!program.reserved() && !operation.barrier.active());
    assert(releaseClockMinute >= 0);
    rtcTime.h = hour; rtcTime.m = 0; releaseClockTick();
    assert(releaseClockEnabled && program.reserved() && program.preset == timiac::Preset::Hour);
    assert(program.targetMinute == hour * 60 && releaseClockMinute == int(hour * 60));
    programTick(); assert(operation.barrier.active());
    const String danceId = program.id;
    releaseClockTick(); assert(program.id == danceId);
    report(0, "preparing", true); report(1, "preparing", true); operationTick();
    report(0, "done"); programTick(); releaseClockTick(); assert(program.state == "running");
    report(1, "done"); programTick(); releaseClockTick();
    assert(program.state == "done" && releaseClockEnabled && armTimeMinutes == int(hour * 60));
    rtcTime.m = 1; releaseClockTick(); assert(program.id == danceId && !operation.barrier.active());
    rtcTime.m = 5; releaseClockTick();
    assert(operation.barrier.active() && !program.reserved() && armTimeMinutes == int(hour * 60 + 5));
  }
  // A gesture can cover the boundary and invalidate the arm pose. The last
  // automatic target survives it, so the hour dance starts once work is idle.
  resetMaster(); connectBoard(0); connectBoard(1);
  releaseStartupPending = false; releaseClockEnabled = true; releaseClockMinute = 115;
  rtcTime.h = 2; rtcTime.m = 0; program.state = "running"; armTimeMinutes = -1;
  for (auto& board : boards) {
    board.homed = 63; board.trajectorySupported = true;
    for (unsigned a = 0; a < 6; ++a) { board.known[a] = true; board.positions[a] = kTravelMm; }
  }
  releaseClockTick(); assert(program.preset != timiac::Preset::Hour);
  program = Program(); rtcTime.m = 1; releaseClockTick();
  assert(program.reserved() && program.preset == timiac::Preset::Hour && program.targetMinute == 121);
  stopAll("Cancel automatic hour"); releaseClockTick();
  assert(!releaseClockEnabled && releaseClockMinute == -1 && !program.reserved());
#endif
  // Next hour queues open, close, then time as one synchronized two-board operation.
  resetMaster(); connectBoard(0); connectBoard(1);
  for (auto& board : boards) {
    board.homed = 63;
    for (unsigned a = 0; a < 6; ++a) { board.known[a] = true; board.positions[a] = kTravelMm; }
  }
  assert(startBuiltinPreset(timiac::Preset::Hour, 210, "hour", 123)); // Old bridges lack the capability.
  boards[0].trajectorySupported = true;
  assert(startBuiltinPreset(timiac::Preset::Hour, 210, "hour", 123));
  boards[1].trajectorySupported = true; boards[0].known[0] = false;
  assert(startBuiltinPreset(timiac::Preset::Hour, 210, "hour", 123));
  boards[0].known[0] = true;
  assert(!startBuiltinPreset(timiac::Preset::Hour, 210, "hour", 123));
  assert(program.total == 1 && program.state == "running");
  programTick(); assert(program.hasStep && operation.barrier.active());
  for (unsigned b = 0; b < 2; ++b) {
    DynamicJsonDocument packet(4096);
    const auto& wire = peers[b].socket.sent.back();
    assert(wire.length() <= timiac::PeerOutbox::maxPacket);
    assert(!deserializeJson(packet, wire));
    assert(packet["type"] == "prepare" && packet["trajectory"] == true);
    assert(packet["lines"].size() == 5); // Three poses bracketed by acceleration settings.
    assert(validLines(packet["lines"], true) && !validLines(packet["lines"]));
    const char* lines[timiac::maxTrajectoryCommands]; unsigned count = 0;
    for (JsonVariant line : packet["lines"].as<JsonArray>()) lines[count++] = line.as<const char*>();
    timiac::OctopusSession kernel;
    kernel.state = timiac::OctopusSession::State::Ready; kernel.identity = true;
    kernel.lastRx = hostNow; kernel.homed = 63; kernel.drivers = boards[b].drivers;
    assert(kernel.stage(lines, count, hostNow, true)); kernel.next(hostNow); assert(kernel.commit());
    unsigned moves = 0, dwells = 0, drains = 0;
    while (kernel.running()) if (const char* line = kernel.next(hostNow)) {
      if (!strncmp(line, "G1 ", 3)) { ++moves; assert(!drains); }
      if (!strncmp(line, "G4 ", 3)) ++dwells;
      if (!strcmp(line, "M400")) ++drains;
      kernel.receive("ok", hostNow);
    }
    // At 03:30 both time arms belong to board 2; board 1 remains closed
    // during the final pose and waits for its partner instead of moving again.
    assert(moves == (b == 0 ? 2U : 3U) && dwells == (b == 0 ? 1U : 0U));
    assert(drains == 1 && kernel.completions == 1);
  }
  report(0, "preparing", true); operationTick(); programTick();
  assert(lastType(0) == "prepare");
  report(1, "preparing", true); operationTick(); programTick();
  assert(lastType(0) == "commit" && lastType(1) == "commit");
  report(0, "done"); programTick(); assert(program.state == "running" && armTimeMinutes == -1);
  report(1, "done"); programTick(); assert(program.state == "done" && armTimeMinutes == 210);
  programTick(); assert(peers[0].socket.sent.size() == 2 && peers[1].socket.sent.size() == 2);

  // All Struggle poses, including the final time, share one queued operation.
  assert(!startBuiltinPreset(timiac::Preset::Struggle, 825, "struggle", 124)); programTick();
  assert(program.total == 1 && program.hasStep && operation.barrier.active());
  const unsigned chosenStruggle = program.struggleVariant;
  const unsigned struggleCommands = timiac::presetCount(timiac::Preset::Struggle, chosenStruggle) + 2;
  assert(chosenStruggle < 7 && program.name.indexOf(timiac::strugglePatterns[chosenStruggle].name) >= 0);
  bool stationary[2][timiac::maxTrajectoryCommands] = {};
  for (unsigned b = 0; b < 2; ++b) {
    // Larger Struggle packets span multiple normal network-loop writes.
    while (peers[b].outbox.pending()) assert(flushPeer(peers[b]));
    DynamicJsonDocument packet(4096);
    assert(!deserializeJson(packet, peers[b].socket.sent.back()));
    assert(packet["type"] == "prepare" && packet["trajectory"] == true);
    assert(packet["lines"].size() == struggleCommands);
    assert(packet["lines"][0] == "M204 T20.000");
    assert(packet["lines"][struggleCommands - 1] == "M204 T25.000");
    const char* lines[timiac::maxTrajectoryCommands]; unsigned count = 0;
    for (JsonVariant line : packet["lines"].as<JsonArray>()) {
      lines[count] = line.as<const char*>();
      stationary[b][count] = !strncmp(lines[count], "G4 ", 3);
      ++count;
    }
    timiac::OctopusSession kernel;
    kernel.state = timiac::OctopusSession::State::Ready; kernel.identity = true;
    kernel.lastRx = hostNow; kernel.homed = 63; kernel.drivers = boards[b].drivers;
    assert(kernel.stage(lines, count, hostNow, true)); kernel.next(hostNow); assert(kernel.commit());
    unsigned frames = 0, drains = 0;
    while (kernel.running()) if (const char* line = kernel.next(hostNow)) {
      if (timiac::trajectoryCommand(line)) { ++frames; assert(!drains); }
      if (!strcmp(line, "M400")) ++drains;
      kernel.receive("ok", hostNow);
    }
    assert(frames == struggleCommands + 1 && drains == 1 && kernel.completions == 1); // includes stage acceleration reset
  }
  for (unsigned i = 0; i < struggleCommands; ++i) assert(!stationary[0][i] || !stationary[1][i]);
  report(0, "preparing", true); operationTick(); programTick();
  report(1, "preparing", true); operationTick(); programTick();
  report(0, "done"); programTick(); assert(program.state == "running");
  report(1, "done"); programTick(); assert(program.state == "done" && armTimeMinutes == 825);
  assert(!startBuiltinPreset(timiac::Preset::Struggle, 825, "struggle-2", 125));
  assert(program.struggleVariant != chosenStruggle && program.struggleVariant < 7);
  program = Program();

  assert(!startBuiltinPreset(timiac::Preset::Hour, 270, "hour-2", 124)); programTick();
  report(0, "preparing", true); report(1, "preparing", true); operationTick();
  stopAll("Cancel queued hour"); programTick();
  assert(program.state == "faulted" && operation.barrier.phase == timiac::Phase::Failed && armTimeMinutes == -1);

  // The hello boot reason describes the ESP boot, not every TCP reconnect.
  resetMaster(); connectBoard(0); const String originalBoot = peers[0].boot;
  peers[0].paired = false;
  DynamicJsonDocument hello(1024);
  hello["type"] = "hello"; hello["key"] = TIMIAC_PEER_KEY; hello["from"] = originalBoot;
  hello["board"] = 1; hello["axes"] = kAxisCount; hello["version"] = timiac::protocolVersion;
  hello["machine"] = timiac::machineId; hello["software"] = kSoftwareRevision; hello["resetReason"] = "External System";
  String text; serializeJson(hello, text); processPeer(peers[0], text);
  assert(logs[(logSequence - 1) % 32].text.indexOf("same ESP boot") >= 0);
  assert(!boards[0].trajectorySupported);
  hello["trajectoryVersion"] = timiac::trajectoryVersion;
  hello["from"] = "new-boot"; peers[0].paired = false; text = ""; serializeJson(hello, text); processPeer(peers[0], text);
  assert(logs[(logSequence - 1) % 32].text.indexOf("boot reason: External System") >= 0);
  assert(boards[0].trajectorySupported);

  // A full send buffer is backpressure, not a disconnected ESP. Preparation
  // remains queued; short writes reconstruct exactly one valid JSON frame.
  resetMaster(); connectBoard(0); peers[0].socket.writeCapacity = 0;
  startHome(0); assert(operation.barrier.active() && peers[0].socket.connected());
  assert(peers[0].outbox.pending() && peers[0].socket.sent.empty());
  for (unsigned i = 0; i < 100 && peers[0].outbox.pending(); ++i) {
    peers[0].socket.writeCapacity = 7; ++hostNow; flushPeer(peers[0]);
  }
  assert(!peers[0].outbox.pending() && peers[0].socket.sent.size() == 1 && lastType(0) == "prepare");
  peers[0].socket.writeCapacity = 4096;
  report(0, "preparing", true); operationTick(); report(0, "done");
  assert(operation.barrier.phase == timiac::Phase::Done && !systemLatched);

  // Stop cancels an unsent command instead of releasing it when Wi-Fi clears.
  resetMaster(); connectBoard(0); peers[0].socket.writeCapacity = 0; startHome(0);
  stopAll("Operator stop"); assert(!peers[0].outbox.pending() && peers[0].socket.connected());
  peers[0].socket.writeCapacity = 4096; networkTick();
  assert(peers[0].socket.sent.size() == 1 && lastType(0) == "stop");

  // Truncating a partial JSON command is invalid: cancellation closes that
  // connection and retains the Stop obligation for its return.
  resetMaster(); connectBoard(0); peers[0].socket.writeCapacity = 5; startHome(0);
  assert(peers[0].outbox.partialFrame); stopAll("Operator stop");
  assert(!peers[0].socket.connected() && !peers[0].outbox.pending() && stopPending);

  // True send failure and persistent congestion still cancel work.
  resetMaster(); connectBoard(0); peers[0].socket.writeSucceeds = false; startHome(0);
  assert(operation.barrier.phase == timiac::Phase::Failed && peers[0].closeReason == "TCP send failed");
  resetMaster(); connectBoard(0); peers[0].socket.writeCapacity = 0; startHome(0);
  hostNow += kPeerTimeout; peers[0].rx = hostNow; networkTick();
  assert(operation.barrier.phase == timiac::Phase::Failed && peers[0].closeReason == "TCP send stalled for 15 seconds");

  // Both directions: real prepare/commit/completion packets for Z/A/B/C.
  for (unsigned b = 0; b < 2; ++b) {
    resetMaster(); connectBoard(b);
    assert(boardsHealthy(1U << b) && !bothHealthy());
    startHome(b); assert(lastType(b) == "prepare" && peers[1 - b].socket.sent.empty());
    report(b, "preparing", true); operationTick();
    assert(lastType(b) == "commit" && operation.barrier.phase == timiac::Phase::Running);
    operationTick(); assert(!systemLatched); // Absent other board does not cancel work.
    report(b, "done"); assert(operation.barrier.phase == timiac::Phase::Done);
    assert(receipts[0].state == "done");

    // Stop/recovery on one connected board needs only its acknowledgement.
    stopAll("Operator stop"); assert(stopRequired == (1U << b) && stopPending);
    hostNow += 251; networkTick(); assert(lastType(b) == "stop");
    report(b, "idle"); assert(stopPending && systemLatched);
    boards[b].stopAck = stopToken; report(b, "idle");
    assert(!stopPending && !systemLatched && boardsHealthy(1U << b));

    // A late board must receive the outstanding Stop before becoming usable.
    connectBoard(1 - b); assert(!boardsHealthy(1U << (1 - b)));
    hostNow += 251; networkTick(); assert(lastType(1 - b) == "stop");
    boards[1 - b].stopAck = stopToken; report(1 - b, "idle"); assert(bothHealthy());
  }

  resetMaster(); connectBoard(0); connectBoard(1); startHome(0);
  report(0, "preparing", true); operationTick();
  report(1, "failed", false, "unused board fault", 1);
  assert(operation.barrier.phase == timiac::Phase::Running && !systemLatched);
  peers[1].socket.stop(); networkTick(); assert(operation.barrier.phase == timiac::Phase::Running);
  report(0, "done"); assert(operation.barrier.phase == timiac::Phase::Done);

  // An unused ESP may be connected without its Octopus. It acknowledges Stop,
  // but its absent motor board must not prevent commissioning the other board.
  resetMaster(); connectBoard(0); connectBoard(1); boards[1].online = false;
  startHome(0); report(0, "preparing", true); operationTick();
  stopAll("Operator stop"); assert(stopRequired == 3 && stopRecoveryRequired == 1);
  boards[0].stopAck = stopToken; report(0, "failed"); assert(stopPending);
  boards[1].stopAck = stopToken; report(1, "idle");
  assert(!systemLatched && !stopPending && boardsHealthy(1) && !bothHealthy());

  // Losing the participating board stops work and cannot recover from a stale
  // acknowledgement or from the absent/nonparticipating board's packets.
  resetMaster(); connectBoard(0); startHome(0); report(0, "preparing", true); operationTick();
  peers[0].socket.stop(); networkTick();
  assert(operation.barrier.phase == timiac::Phase::Failed && stopRequired == 1 && stopPending);
  recoverStop(); assert(systemLatched);
  connectBoard(0); report(0, "done"); assert(systemLatched && stopPending);
  boards[0].stopAck = stopToken; report(0, "done");
  assert(!systemLatched && !stopPending && operation.barrier.phase == timiac::Phase::Failed);
  assert(lastType(0) == "commit"); // No automatic prepare/commit on reconnection.

  // Program/default barriers still require both boards even for one-board lines.
  resetMaster(); connectBoard(0); connectBoard(1); startHome(0, true);
  assert(lastType(0) == "prepare" && lastType(1) == "prepare");
  report(0, "preparing", true); operationTick(); assert(lastType(0) == "prepare");
  report(1, "preparing", true); operationTick(); assert(lastType(0) == "commit" && lastType(1) == "commit");
  report(0, "done"); assert(operation.barrier.active());
  report(1, "done"); assert(operation.barrier.phase == timiac::Phase::Done);

  resetMaster(); connectBoard(0); startHome(0, true);
  assert(operation.barrier.phase == timiac::Phase::Failed && stopRequired == 3);
  boards[0].stopAck = stopToken; report(0, "done"); assert(stopPending && systemLatched);

  resetMaster(); stopAll("Stop with no boards"); recoverStop();
  assert(!stopPending && !systemLatched);
  connectBoard(0); assert(!boardsHealthy(1));
  hostNow += 251; networkTick(); assert(lastType(0) == "stop");
  boards[0].stopAck = stopToken; report(0, "idle"); assert(boardsHealthy(1));
  // Exercise the production GPIO adapter and gesture path, not just the gate.
  // The old ultrasonic pins must never be configured/read by this driver.
  resetMaster(); connectBoard(0); connectBoard(1);
  releaseStartupPending = false; releaseClockEnabled = true;
  for (auto& board : boards) {
    board.homed = 63; board.trajectorySupported = true;
    for (unsigned arm = 0; arm < 6; ++arm) { board.known[arm] = true; board.positions[arm] = kTravelMm; }
  }
  auto sensorFor = [&](unsigned level, unsigned duration) {
    photoelectricLevel = level;
    for (unsigned elapsed = 0; elapsed < duration; elapsed += 20) {
      hostNow += 20;
      for (unsigned b = 0; b < 2; ++b) { boards[b].stateAt = hostNow; peers[b].rx = hostNow; }
      proximityTick();
    }
  };
  sensorFor(50, 600); assert(!proximityTriggers && !proximityGate.armed && !proximityKnown); // Boot in deadband.
  sensorFor(0, 600); assert(!proximityTriggers && !proximityGate.armed); // Held at boot / grounded.
  assert(proximityLastOutcome == "ignored" && proximityLastReason.indexOf("not armed") >= 0 && proximityIgnored == 1);
  const unsigned ignoredAtBoot = proximityIgnored;
  sensorFor(0, 600); assert(proximityIgnored == ignoredAtBoot); // One event for a held hand.
  {
    DynamicJsonDocument status(1024); writeProximityStatus(status.to<JsonObject>());
    assert(status["rawAdc"] == 0 && status["millivolts"].isNull() && status["adcFloorMv"] == 75);
    assert(status["belowRange"] == true && status["detected"] == true);
  }
  sensorFor(95, 600); assert(proximityGate.armed);
  {
    DynamicJsonDocument status(1024); writeProximityStatus(status.to<JsonObject>());
    assert(status["type"] == "photoelectric" && status["pin"] == 34 && status["inputMode"] == "analog");
    assert(status["detected"] == false && status["rawHigh"].isNull() && status["distanceCm"].isNull());
    assert(status["millivolts"] == 95 && status["rawAdc"] == 380 && status["belowRange"] == true);
    assert(status["detectBelowMv"] == 40 && status["clearAboveMv"] == 60);
  }
  sensorFor(50, 300); sensorFor(40, 300); sensorFor(60, 300);
  assert(proximityGate.armed && !proximityDetected && !proximityTriggers); // Hysteresis, including exact boundaries.
  sensorFor(39, 180); assert(!proximityTriggers);
  sensorFor(0, 60); assert(proximityTriggers == 1 && program.reserved() && program.preset == timiac::Preset::Struggle);
  assert(proximityLastOutcome == "triggered" && proximityLastReason.isEmpty());
  assert(proximityLastTriggerTime == rtcText && proximityLastTriggerId == program.id);
  const String firstSensorMotion = proximityLastTriggerId, firstSensorTime = proximityLastTriggerTime;
  assert(logs[(logSequence - 1) % 32].text.indexOf("Hand sensor: Struggle triggered") == 0);
  assert(program.struggleVariant < 7 && program.targetMinute == 13 * 60 + 45 && releaseClockEnabled);
  sensorFor(50, 600); assert(proximityTriggers == 1 && !proximityGate.armed && proximityDetected);
  sensorFor(95, 600); sensorFor(0, 300); // A new arrival during the animation is consumed with its reason.
  assert(proximityLastOutcome == "ignored" && proximityLastReason == "Motion or a program is busy");
  assert(proximityLastTriggerId == firstSensorMotion && proximityLastTriggerTime == firstSensorTime);
  program = Program(); operation = Operation();
  sensorFor(0, 600); assert(proximityTriggers == 1); // No replay after movement.
  sensorFor(95, 600); assert(proximityGate.armed);
  photoelectricAdcValid = false; sensorFor(0, 300);
  assert(!proximityGate.armed && !proximityKnown && proximityTriggers == 1);
  { DynamicJsonDocument status(1024); writeProximityStatus(status.to<JsonObject>()); assert(status["detected"].isNull() && status["rawAdc"].isNull()); }
  photoelectricAdcValid = true; sensorFor(0, 300); assert(!proximityGate.armed && proximityTriggers == 1);
  sensorFor(95, 600); assert(proximityGate.armed);
  stopRequested = true; sensorFor(0, 300); assert(proximityTriggers == 1 && !proximityGate.armed);
  assert(proximityLastReason == "Stop is active");
  stopRequested = false; sensorFor(0, 300); assert(proximityTriggers == 1);
  boards[0].homed = 0; sensorFor(95, 600); assert(!proximityGate.armed);
  {
    DynamicJsonDocument status(1024); writeProximityStatus(status.to<JsonObject>());
    assert(status["detected"] == false && status["ready"] == false); // Still visible before home.
    hostNow += 1001; status.clear(); writeProximityStatus(status.to<JsonObject>());
    assert(status["detected"].isNull()); // Stale samples do not look current.
  }
  sensorFor(0, 300); assert(proximityLastReason == "Arms need homing");
  const unsigned ignoredBeforeHeld = proximityIgnored;
  sensorFor(0, 1000); assert(proximityIgnored == ignoredBeforeHeld);
  assert(proximityLastTriggerId == firstSensorMotion && proximityLastTriggerTime == firstSensorTime);
  {
    DynamicJsonDocument status(2048); writeProximityStatus(status.to<JsonObject>());
    assert(!status.overflowed() && status["lastOutcome"] == "ignored" && status["lastReason"] == "Arms need homing");
    assert(status["lastTriggerTime"] == rtcText && status["triggers"] == 1 && status["lastTriggerAgeMs"].as<unsigned>() > 0);
  }
  sensorFor(95, 600);
  boards[0].homed = 63; sensorFor(95, 600); assert(proximityGate.armed);
  hostNow += 1000; sensorFor(0, 20); assert(!proximityGate.armed && proximityTriggers == 1);
  sensorFor(0, 300); assert(proximityTriggers == 1); // Long sampling gap discards pending eligibility.
#if TIMIAC_RELEASE
  releaseClockEnabled = false; sensorFor(95, 600); assert(!proximityGate.armed); // Manual Home/paused clock.
  sensorFor(0, 300); assert(proximityLastReason == "Clock paused; start the clock first" && proximityTriggers == 1);
#endif
  resetMaster(); connectBoard(0); connectBoard(1); releaseClockEnabled = true;
  rtcValid = false; sensorFor(95, 600); sensorFor(0, 300);
  assert(proximityLastReason == "Clock time is invalid" && proximityLastEventTime.isEmpty());
  resetMaster(); connectBoard(0); releaseClockEnabled = true;
  sensorFor(95, 600); sensorFor(0, 300);
  assert(proximityLastReason == "Motor boards are not ready" && !proximityTriggers);
  resetMaster(); connectBoard(0); connectBoard(1);
  firmwareUpdating = true;
  assert(!bothHealthy() && !boardsHealthy(1));
  stopAll("Operator cancelled a firmware update"); assert(firmwareUpdateCancel);
  resetMaster(); connectBoard(0); connectBoard(1);
  boards[0].updating = true;
  assert(!boardOnline(0) && !bothHealthy() && boardsHealthy(2));
  // Retain the originating gate failure. A homing Stop subsequently replaces
  // board diagnostics with M112/reset-required and used to hide this evidence.
  for (unsigned gate = 0; gate < 10; ++gate) {
    resetMaster(); connectBoard(0); connectBoard(1); startHome(0);
    operation.barrier.phase = timiac::Phase::Running;
    const char* expected = "";
    switch (gate) {
      case 0: boards[0].online = false; boards[0].diagnostic = "Waiting for G28 X (210 ms)"; expected = "Octopus unavailable"; break;
      case 1: boards[0].drivers.currentOk = 62; expected = "current=62"; break;
      case 2: boards[0].drivers.thermalWarning = 1; expected = "temperature warning"; break;
      case 3: boards[0].latched = true; expected = "safety stop"; break;
      case 4: boards[0].fault = "UART failure"; expected = "UART failure"; break;
      case 5: stopToken = "existing-stop"; expected = "Stop acknowledgement"; break;
      case 6: boards[0].stateAt = hostNow - kPeerTimeout; expected = "status overdue"; break;
      case 7: peers[0].socket.stop(); expected = "ESP connection lost"; break;
      case 8: networkChanging = true; expected = "Router connection changed"; break;
      case 9: firmwareUpdating = true; expected = "Firmware update became active"; break;
    }
    assert(!boardsHealthy(1)); operationTick();
    assert(operation.barrier.phase == timiac::Phase::Failed && systemLatched);
    assert(systemFault.indexOf(expected) >= 0 && lastStopReason == systemFault);
  }
  // A recovered stop must leave its cause visible after calibration and clock
  // resume; transient systemFault clearing previously erased all evidence.
  resetMaster(); connectBoard(0); connectBoard(1); releaseClockEnabled = true;
  stopAll("Bridge link lost during work; movement cancelled, no replay");
  assert(lastStopCount == 1 && lastStopReason == systemFault && !releaseClockEnabled);
  for (unsigned b = 0; b < 2; ++b) boards[b].stopAck = stopToken;
  recoverStop();
  assert(!systemLatched && systemFault.isEmpty() && !lastStopReason.isEmpty());
  trackManualClockAction("show-time");
  DynamicJsonDocument history(1024); writeReliabilityStatus(history.to<JsonObject>());
  assert(history["lastStop"] == lastStopReason && history["stopCount"] == 1);
  assert(lastStopSession == bootId);
#if TIMIAC_RELEASE
  assert(clockPauseReason.isEmpty());
#endif
  std::cout << "Production coordinator: independent boards, jobs, Stop, photoelectric gestures and update motion guards passed\n";
  return 0;
}
