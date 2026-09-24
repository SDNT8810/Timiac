#define TIMIAC_MASTER 1
#define TIMIAC_RELEASE 0
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
WiFiServer listener;
#include "../ESP_Timiac/src/runtime.h"
#include "../ESP_Timiac/src/uart_bridge.h"
#include "../ESP_Timiac/src/peer_link.h"

void resetMaster() {
  hostNow = 1000; bootId = "master-boot"; stopToken = stopReason = systemFault = "";
  systemLatched = stopPending = stopRequested = networkChanging = false; stopRequired = stopRecoveryRequired = 0;
  operation = Operation(); program = Program(); receiptNext = 0;
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
int main() {
  // The hello boot reason describes the ESP boot, not every TCP reconnect.
  resetMaster(); connectBoard(0); const String originalBoot = peers[0].boot;
  peers[0].paired = false;
  DynamicJsonDocument hello(1024);
  hello["type"] = "hello"; hello["key"] = TIMIAC_PEER_KEY; hello["from"] = originalBoot;
  hello["board"] = 1; hello["axes"] = kAxisCount; hello["version"] = timiac::protocolVersion;
  hello["machine"] = timiac::machineId; hello["software"] = kSoftwareRevision; hello["resetReason"] = "External System";
  String text; serializeJson(hello, text); processPeer(peers[0], text);
  assert(logs[(logSequence - 1) % 32].text.indexOf("same ESP boot") >= 0);
  hello["from"] = "new-boot"; peers[0].paired = false; text = ""; serializeJson(hello, text); processPeer(peers[0], text);
  assert(logs[(logSequence - 1) % 32].text.indexOf("boot reason: External System") >= 0);

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
  std::cout << "Production coordinator: independent boards, coordinated jobs, fault isolation, Stop and no replay passed\n";
}
