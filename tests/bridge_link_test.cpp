#define TIMIAC_MASTER 0
#define TIMIAC_RELEASE 0
#define TIMIAC_BOARD_ID 1
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
WiFiServer listener;
#include "../ESP_Timiac/src/runtime.h"
#include "../ESP_Timiac/src/uart_bridge.h"
#include "../ESP_Timiac/src/peer_link.h"

void setupBridge() {
  hostNow = 4000; bootId = "bridge-boot"; coordinatorBoot = "master-boot";
  systemLatched = false; systemFault = ""; operation = Operation();
  peers[0] = Peer(); auto& p = peers[0]; p.boot = coordinatorBoot; p.paired = true;
  p.socket.connectedFlag = true; p.rx = p.tx = hostNow;
  localUart().session = timiac::OctopusSession();
  localUart().session.identity = true; localUart().session.state = timiac::OctopusSession::State::Ready;
  localUart().session.lastRx = hostNow; localUart().session.homed = 1;
  localUart().drivers.tested = localUart().drivers.connected = localUart().drivers.currentOk = localUart().drivers.stepsOk = localUart().drivers.modeOk = 63;
  localUart().drivers.columns = true; localUart().drivers.settings = 7;
  for (unsigned i = 0; i < 6; ++i) {
    localUart().drivers.currentMa[i] = timiac::driverCurrent; localUart().drivers.microsteps[i] = timiac::driverMicrosteps;
  }
  localUart().fault = ""; localUart().halted = localUart().job = false;
  localUart().seenFailures = localUart().seenCompletions = 0; Serial.sent.clear();
}
void drainNetwork() {
  for (unsigned i = 0; peers[0].outbox.pending() && i < 100; ++i) {
    peers[0].socket.writeCapacity = 37; ++hostNow; flushPeer(peers[0]);
  }
  assert(!peers[0].outbox.pending() && peers[0].socket.connected());
}
void validPackets() {
  for (const auto& text : peers[0].socket.sent) {
    DynamicJsonDocument packet(4096); assert(!deserializeJson(packet, text));
    assert(packet["from"] == bootId && packet["session"] == coordinatorBoot);
  }
}
int main() {
  // Reconnecting during an OTA upload replays the existing Stop fence. It is
  // an acknowledgement request, not a new operator cancellation.
  setupBridge(); acknowledgedStop = "ota-stop"; firmwareUpdating = true; firmwareUpdateCancel = false;
  DynamicJsonDocument stop(512); stop["type"] = "stop"; stop["token"] = acknowledgedStop;
  stop["from"] = coordinatorBoot; stop["session"] = coordinatorBoot;
  String stopPacket; serializeJson(stop, stopPacket); processPeer(peers[0], stopPacket);
  assert(!firmwareUpdateCancel);
  stop["token"] = "new-operator-stop"; stopPacket = ""; serializeJson(stop, stopPacket);
  processPeer(peers[0], stopPacket); assert(firmwareUpdateCancel);
  firmwareUpdating = firmwareUpdateCancel = false;
  // Idle status and bursts of UART log lines can exhaust the ESP8266 TCP
  // buffer. The old whole-packet writer closed the connection in this case.
  setupBridge(); auto& p = peers[0]; p.socket.writeCapacity = 0;
  sendState(); const size_t pending = p.outbox.pending(); assert(pending);
  for (unsigned i = 0; i < 80; ++i) { logMessage("UART diagnostic line"); sendState(); }
  assert(p.socket.connected() && p.outbox.pending() == pending && !systemLatched);
  drainNetwork(); validPackets();
  assert(p.socket.sent.size() == 1);
  DynamicJsonDocument settings(4096); assert(!deserializeJson(settings, p.socket.sent.back()));
  for (unsigned i = 0; i < 6; ++i) {
    assert(settings["board"]["drivers"]["currentMa"][i] == timiac::driverCurrent);
    assert(settings["board"]["drivers"]["microstepValues"][i] == timiac::driverMicrosteps);
  }

  // Exercise the real M114 -> completion -> sendState path with no send space.
  setupBridge(); p.socket.writeCapacity = 0;
  operation.id = "home-X1"; operation.barrier.begin(hostNow); operation.barrier.phase = timiac::Phase::Running;
  const char* commands[] = {"G1 X1 F120"}; assert(localUart().session.stage(commands, 1, hostNow));
  localUart().session.next(++hostNow); assert(localUart().session.commit()); localUart().sync();
  while (localUart().session.running()) {
    const char* command = localUart().session.next(++hostNow);
    if (command) {
      localUart().send(command);
      if (!strcmp(command, "M114")) localUart().receive("X:1.00 Y:0 Z:0 A:0 B:0 C:0 Count X:720 Y:0 Z:0 A:0 B:0 C:0");
      localUart().receive("ok");
    }
    localUart().sync();
  }
  assert(operation.barrier.phase == timiac::Phase::Done && localUart().homed == 1 && !systemLatched && p.socket.connected());
  drainNetwork(); p.socket.writeCapacity = 4096; sendState(); drainNetwork(); validPackets();
  DynamicJsonDocument done(4096); assert(!deserializeJson(done, p.socket.sent.back()));
  assert(done["type"] == "state" && done["phase"] == "done" && done["board"]["homed"] == 1);
  assert(localUart().session.resets == 0 && Serial.sent.find("M112") == std::string::npos && Serial.sent.find("M410") == std::string::npos);

  // The production peer decoder and UART adapter accept an advertised queued
  // trajectory, preserve its order, and wait for one final planner drain only.
  setupBridge(); localUart().session.homed = 63;
  DynamicJsonDocument trajectory(4096);
  trajectory["type"] = "prepare"; trajectory["from"] = coordinatorBoot; trajectory["session"] = coordinatorBoot;
  trajectory["speedPercent"] = 20; trajectory["id"] = "hour"; trajectory["hash"] = 123; trajectory["trajectory"] = true;
  JsonArray lines = trajectory.createNestedArray("lines");
  for (unsigned i = 0; i < 28; ++i) lines.add("G1 X1 F120");
  String packet; serializeJson(trajectory, packet); processPeer(p, packet);
  localUart().session.next(hostNow); assert(localUart().prepared());
  trajectory.clear(); trajectory["type"] = "commit"; trajectory["speedPercent"] = 20; trajectory["id"] = "hour";
  trajectory["from"] = coordinatorBoot; trajectory["session"] = coordinatorBoot;
  packet = ""; serializeJson(trajectory, packet); processPeer(p, packet);
  assert(operation.speedPercent == 20 && localUart().session.speedPercent == 20);
  assert(localUart().job && operation.barrier.phase == timiac::Phase::Running);
  unsigned moves = 0, drains = 0;
  while (localUart().session.running()) {
    if (const char* command = localUart().session.next(++hostNow)) {
      if (!strcmp(command, "M220 S20")) assert(moves == 0);
      if (!strncmp(command, "G1 ", 3)) { ++moves; assert(!drains); }
      if (!strcmp(command, "M400")) ++drains;
      localUart().send(command); localUart().receive("ok");
    }
    localUart().sync();
  }
  assert(moves == 28 && drains == 1 && operation.barrier.phase == timiac::Phase::Done && !systemLatched);

  // An actual broken connection during motion still invokes the bridge stop.
  setupBridge(); operation.id = "move"; operation.barrier.begin(hostNow); operation.barrier.phase = timiac::Phase::Running;
  p.socket.writeSucceeds = false; sendState(); networkTick();
  assert(operation.barrier.phase == timiac::Phase::Failed && systemLatched);
  assert(Serial.sent.find("M410") != std::string::npos && !localUart().homed && !p.outbox.pending());
  assert(p.socket.discarded);
  // A timed-out idle socket is discarded and a new status-only handshake can
  // start on the next retry, without waiting for the old TCP retransmissions.
  setupBridge(); p.rx = hostNow - kPeerTimeout; reconnectAt = hostNow;
  networkTick(); assert(!p.paired && !p.socket.connected() && p.socket.discarded == 1);
  assert(!systemLatched && Serial.sent.find("M410") == std::string::npos);
  p.socket.connectSucceeds = true; hostNow += 5001; networkTick();
  assert(p.socket.connected() && !p.paired && p.socket.sent.back().indexOf("hello") >= 0);
  std::cout << "Production bridge: congested telemetry, partial writes, M114 completion, and genuine link failure passed\n";
  return 0;
}
