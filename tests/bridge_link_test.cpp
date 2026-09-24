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
  uart.session = timiac::OctopusSession();
  uart.session.identity = true; uart.session.state = timiac::OctopusSession::State::Ready;
  uart.session.lastRx = hostNow; uart.session.homed = 1;
  uart.drivers.tested = uart.drivers.connected = uart.drivers.currentOk = uart.drivers.stepsOk = uart.drivers.modeOk = 63;
  uart.drivers.columns = true; uart.drivers.settings = 7;
  uart.fault = ""; uart.halted = uart.job = false;
  uart.seenFailures = uart.seenCompletions = 0; Serial.sent.clear();
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
  // Idle status and bursts of UART log lines can exhaust the ESP8266 TCP
  // buffer. The old whole-packet writer closed the connection in this case.
  setupBridge(); auto& p = peers[0]; p.socket.writeCapacity = 0;
  sendState(); const size_t pending = p.outbox.pending(); assert(pending);
  for (unsigned i = 0; i < 80; ++i) { logMessage("UART diagnostic line"); sendState(); }
  assert(p.socket.connected() && p.outbox.pending() == pending && !systemLatched);
  drainNetwork(); validPackets();
  assert(p.socket.sent.size() == 1);

  // Exercise the real M114 -> completion -> sendState path with no send space.
  setupBridge(); p.socket.writeCapacity = 0;
  operation.id = "home-X1"; operation.barrier.begin(hostNow); operation.barrier.phase = timiac::Phase::Running;
  const char* commands[] = {"G1 X1 F180"}; assert(uart.session.stage(commands, 1, hostNow));
  uart.session.next(++hostNow); assert(uart.session.commit()); uart.sync();
  while (uart.session.running()) {
    const char* command = uart.session.next(++hostNow);
    if (command) {
      uart.send(command);
      if (!strcmp(command, "M114")) uart.receive("X:1.00 Y:0 Z:0 A:0 B:0 C:0 Count X:720 Y:0 Z:0 A:0 B:0 C:0");
      uart.receive("ok");
    }
    uart.sync();
  }
  assert(operation.barrier.phase == timiac::Phase::Done && uart.homed == 1 && !systemLatched && p.socket.connected());
  drainNetwork(); p.socket.writeCapacity = 4096; sendState(); drainNetwork(); validPackets();
  DynamicJsonDocument done(4096); assert(!deserializeJson(done, p.socket.sent.back()));
  assert(done["type"] == "state" && done["phase"] == "done" && done["board"]["homed"] == 1);
  assert(uart.session.resets == 0 && Serial.sent.find("M112") == std::string::npos && Serial.sent.find("M410") == std::string::npos);

  // An actual broken connection during motion still invokes the bridge stop.
  setupBridge(); operation.id = "move"; operation.barrier.begin(hostNow); operation.barrier.phase = timiac::Phase::Running;
  p.socket.writeSucceeds = false; sendState(); networkTick();
  assert(operation.barrier.phase == timiac::Phase::Failed && systemLatched);
  assert(Serial.sent.find("M410") != std::string::npos && !uart.homed && !p.outbox.pending());
  std::cout << "Production bridge: congested telemetry, partial writes, M114 completion, and genuine link failure passed\n";
}
