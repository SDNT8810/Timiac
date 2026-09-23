#pragma once
#include "../../shared/octopus_session.h"

// Arduino adapter. The tested kernel owns all UART state and acknowledgements.
struct UartBridge {
  timiac::OctopusSession session;
  timiac::EndstopReport& endstops = session.endstops;
  timiac::DriverReport& drivers = session.drivers;
  uint8_t& homed = session.homed;
  float (&positions)[6] = session.positions;
  bool (&known)[6] = session.known;
  uint32_t& resetCount = session.resets;
  String fault;
  bool halted = false, job = false;
  uint32_t monitorAt = 0, seenFailures = 0, seenCompletions = 0;
  bool online() const { return session.online(millis()); }
  bool healthy() const { return session.healthy(millis()); }
  bool prepared() const { return session.prepared(); }
  void monitorEndstops(bool enabled) { session.monitor = enabled; monitorAt = millis(); }
  void sync() {
    fault = session.fault; halted = session.halted(); job = session.running();
    if (session.failures != seenFailures) {
      seenFailures = session.failures;
      stopAll(fault);
    }
    if (session.completions != seenCompletions) {
      seenCompletions = session.completions;
      finishLocal();
    }
    if (session.state == timiac::OctopusSession::State::Ready && session.healthy(millis()) && !operation.barrier.active()) {
      systemLatched = false; systemFault = "";
    }
  }
  void send(const char* line) {
#if !TIMIAC_MASTER
    Serial.print(line); Serial.print('\n');
#endif
    if (session.running()) logMessage(String(TIMIAC_BOARD_ID == 1 ? "1 > " : "2 > ") + line);
  }
  void abort(const String& reason) {
    if (operation.barrier.active()) {
      operation.barrier.fail(); operation.error = reason; recordReceipt();
    }
    const char* command = session.stop(millis());
    if (command) send(command);
    fault = session.fault; halted = session.halted(); job = false;
    seenFailures = session.failures;
    logMessage("Stop: " + reason);
  }
  void stage(JsonArray lines) {
    const char* commands[6]; unsigned count = 0;
    for (JsonVariant value : lines) commands[count++] = value.as<const char*>();
    if (!session.stage(commands, count, millis())) {
      session.fail("Preparation refused: driver, firmware, axis or position not ready"); sync();
    }
  }
  void commit() {
    if (!session.commit()) session.fail("Commit refused: no completed preparation");
    sync();
  }
  void receive(const String& line) {
    if (session.running() || line.indexOf("FIRMWARE_NAME:") >= 0 || line.indexOf("Error:") >= 0 || line.startsWith("echo:Bad "))
      logMessage(String(TIMIAC_BOARD_ID == 1 ? "1 < " : "2 < ") + line);
    session.receive(line.c_str(), millis()); sync();
  }
  void tick() {
#if !TIMIAC_MASTER
    if (Serial.hasOverrun()) { session.fail("Octopus serial receive overrun"); sync(); }
    unsigned budget = 4096;
    while (Serial.available() && budget--) {
      const char c = Serial.read();
      if (c == '\n') {
        if (serialDiscard) { session.fail("Octopus serial line too long"); sync(); }
        else receive(serialLine);
        serialLine = ""; serialDiscard = false;
      } else if (c != '\r' && !serialDiscard) {
        if (serialLine.length() < 512) serialLine += c;
        else serialDiscard = true;
      }
    }
    if (timiac::expired(millis(), monitorAt, kPeerTimeout)) session.monitor = false;
    const char* command = session.next(millis());
    if (command) send(command);
    sync();
#endif
  }
} uart;
