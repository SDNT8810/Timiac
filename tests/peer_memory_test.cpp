// Force ArduinoJson's heap pool allocation to fail, as recorded on the clock.
// Other host allocations remain available so the decoder itself is isolated.
#include "host/Arduino.h"
#include <cstdlib>
#include <iostream>
#include <cmath>
#define ARDUINOJSON_ENABLE_ARDUINO_STRING 1
static bool rejectJsonPool = false;
static void* jsonPoolAllocate(size_t size) { return rejectJsonPool ? nullptr : std::malloc(size); }
#define malloc jsonPoolAllocate
#include <ArduinoJson.h>
#undef malloc
#define main coordinatorRegressionMain
#include "coordinator_test.cpp"
#undef main

int main() {
  resetMaster(); connectBoard(0); connectBoard(1);
  boards[0].software = "2026-10-17.1";
  boards[0].firmware = timiac::machineId;
  boards[0].diagnostic = "Octopus identified; last reply 2715 ms ago: ok";
  boards[0].driverReportAvailable = true; boards[0].driverReportAt = hostNow;
  boards[0].homed = 63;
  for (unsigned axis = 0; axis < 6; ++axis) {
    boards[0].known[axis] = true; boards[0].positions[axis] = 50;
    boards[0].drivers.currentMa[axis] = 1000; boards[0].drivers.microsteps[axis] = 64;
  }
  DynamicJsonDocument packet(4096);
  packet["type"] = "state"; packet["from"] = peers[0].boot; packet["session"] = bootId;
  packet["phase"] = "idle"; packet["latched"] = false; packet["stopAck"] = "";
  writeBoard(packet.createNestedObject("board"), 0);
  assert(!packet.overflowed());
  String text; serializeJson(packet, text);
  rejectJsonPool = true;
  {
    // Reproduce the recorded NoMemory error with the old per-packet pool.
    DynamicJsonDocument oldPool(4096);
    assert(deserializeJson(oldPool, text) == DeserializationError::NoMemory);
  }
  for (unsigned n = 0; n < 1000; ++n) {
    ++hostNow; processPeer(peers[0], text);
    assert(peers[0].socket.connected() && boards[0].stateAt == hostNow);
    assert(boards[0].homed == 63 && boards[0].drivers.good() && boards[0].positions[5] == 50);
    assert(!systemLatched);
  }
  // Reusing the pool cannot retain fields from the preceding board packet.
  processPeer(peers[1], "{\"type\":\"ping\",\"from\":\"bridge-1\",\"session\":\"master-boot\"}");
  assert(peers[1].socket.connected() && !systemLatched);
  rejectJsonPool = false;
  // Malformed input still closes the link; the existing safety stop remains.
  startHome(0); report(0, "preparing", true); operationTick();
  assert(operation.barrier.phase == timiac::Phase::Running);
  processPeer(peers[0], "{invalid}"); operationTick();
  assert(!peers[0].socket.connected() && systemLatched);
  assert(lastStopReason == "Board 1: ESP connection lost");
  std::cout << "Peer decoder: recorded NoMemory reproduced; 1000 valid packets survive failed heap-pool allocation; invalid packet still stops motion\n";
}
