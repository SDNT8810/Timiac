#pragma once
struct UartBridge {
  timiac::EndstopReport endstops;
  bool endstopMonitoring = false;
  uint32_t endstopMonitorAt = 0;
  void monitorEndstops(bool enabled) { endstopMonitoring = enabled; endstopMonitorAt = millis(); }
  String fault, commands[kMaxCommands + 4], active;
  bool ready = false, waiting = false, staged = false, job = false;
  bool everReady = false, healthSeen = false;
  bool halted = false;
  bool releasesAxes = false;
  unsigned count = 0, next = 0;
  uint8_t homed = 0, homingMask = 0;
  uint32_t sent = 0, lastPoll = 0, lastHealth = 0, resetCount = 0;
  float positions[6] = {};
  bool known[6] = {};
  uint32_t healthTimeout() const {
    // Marlin sends `ok` only when each command has finished. Let the command
    // acknowledgement limit supervise every active move, including G28.
    return job ? timiac::acknowledgementTimeout(active.c_str()) : kHealthTimeout;
  }
  bool online() const { return ready && healthSeen && !timiac::expired(millis(), lastHealth, healthTimeout()); }
  bool healthy() const { return online() && fault.isEmpty(); }
  void write(const String& line) {
    active = line; waiting = true; sent = millis();
    if (line == "M119") endstops.begin();
    // Marlin accepts X/Y/Z/A/B/C as configured by AXIS4/5/6_NAME.
    // I/J/K are internal driver identifiers, not replacement G-code letters.
    Serial.print(line); Serial.print('\n');
    if (job) logMessage(String(TIMIAC_BOARD_ID == 1 ? "1 > " : "2 > ") + line);
  }
  void invalidate() {
    endstops.clear();
    ready = waiting = staged = job = healthSeen = false;
    count = next = 0; homed = homingMask = 0; releasesAxes = false;
    for (auto& knownPosition : known) knownPosition = false;
  }
  // Stop only cancels work still queued in this bridge. It deliberately does
  // not send M112, M410, a reset, or any other recovery command to Marlin.
  void abort(const String& reason) {
    endstops.clear();
    count = next = 0; waiting = staged = job = false;
    homed = homingMask = 0; releasesAxes = false;
    if (operation.barrier.active()) {
      operation.barrier.fail(); operation.error = reason; recordReceipt();
    }
    // Cancelling a queue does not repair a Marlin fault or release kill().
    lastPoll = millis();
    logMessage("Queued work cancelled: " + reason);
  }
  void fail(const String& reason, bool resetRequired = false) {
    // Keep the first useful cause (e.g. Bad A Endstop), not a later timeout.
    if (fault.isEmpty()) fault = reason;
    halted |= resetRequired;
    invalidate();
    stopAll(fault);
  }
  void stage(JsonArray lines) {
    count = next = 0; homingMask = 0; releasesAxes = false;
    commands[count++] = "G21"; commands[count++] = "G90";
    for (JsonVariant value : lines) {
      const String command = value.as<String>();
      commands[count++] = command;
      if (timiac::homingCommand(command.c_str())) {
        for (unsigned axis = 0; axis < kAxisCount; ++axis)
          if (command.indexOf(kAxes[axis]) >= 0) homingMask |= uint8_t(1U << axis);
      }
      if (command == "M18" || command == "M84") releasesAxes = true;
    }
    // M400 waits for all moves before accepting the operation as complete.
    // Position reporting remains available as an explicit developer command;
    // it is not part of normal startup, homing, or health checking.
    commands[count++] = "M400";
    staged = true; // No command is transmitted until both ESPs report prepared.
  }
  bool prepared() const { return staged && !waiting && healthy(); }
  void commit() { staged = false; job = true; endstops.clear(); }
  void receive(String line) {
    line.trim(); if (line.isEmpty()) return;
    if (waiting && active == "M119") endstops.receive(line.c_str());
    if (line == "start") {
      const bool interrupted = everReady && operation.barrier.active();
      if (everReady) {
        logMessage(String(TIMIAC_BOARD_ID == 1 ? "1" : "2") + " Octopus reboot detected");
      }
      halted = false; fault = "";
      ++resetCount; invalidate(); lastPoll = millis();
      if (interrupted) stopAll("Octopus rebooted; program position is uncertain");
      return;
    }
    if (line.startsWith("echo:Bad ") && line.indexOf(" Endstop?") == 10 && strchr(kAxes, line[9])) {
      fail(String(line[9]) + (TIMIAC_BOARD_ID == 1 ? "1" : "2") +
        " endstop stayed triggered after homing backoff. Check switch/motor, then reset Octopus.", true);
      return;
    }
    if (line.indexOf("Printer halted") >= 0 || line.indexOf("kill() called") >= 0) {
      fail("Marlin halted: " + line + " Reset Octopus after checking the fault.", true);
      return;
    }
    // A queued/late ok cannot release a hard halt. Only a board restart can.
    if (halted) return;
    int countAt = line.indexOf(" Count");
    String coordinates = countAt >= 0 ? line.substring(0, countAt) : line;
    for (unsigned i = 0; i < 6; ++i) {
      int at = coordinates.indexOf(String(kAxes[i]) + ':');
      if (at >= 0 && (at == 0 || coordinates[at - 1] == ' ')) {
        const char* begin = coordinates.c_str() + at + 2; char* end;
        float value = strtof(begin, &end);
        if (end != begin && isfinite(value)) { positions[i] = value; known[i] = true; }
      }
    }
    bool error = line.startsWith("Error:") || line.startsWith("!!") || line.startsWith("Resend:") ||
      line.indexOf("Unknown command") >= 0 || (line.indexOf("Home ") >= 0 && line.indexOf(" First") >= 0);
    if (error) {
      fail(String(TIMIAC_BOARD_ID == 1 ? "Octopus 1: " : "Octopus 2: ") + line);
      return;
    }
    if (job || (line.indexOf("FIRMWARE_NAME:") >= 0))
      logMessage(String(TIMIAC_BOARD_ID == 1 ? "1 < " : "2 < ") + line);
    if (line == "ok" || line.startsWith("ok ")) {
      lastHealth = millis(); healthSeen = true;
    }
    if ((line == "ok" || line.startsWith("ok ")) && waiting) {
      if (active == "M119") endstops.finish(millis());
      waiting = false;
      if (!ready) {
        // A standard Marlin `ok` is sufficient proof that this bridge and its
        // Octopus UART are connected. Do not require an M114 axis report.
        fault = ""; ready = everReady = true;
        // A fresh handshake after a restart/link recovery clears the local
        // fault latch. Cancelled operations remain failed and are never replayed.
        systemLatched = false; systemFault = "";
        logMessage(String(TIMIAC_BOARD_ID == 1 ? "1" : "2") + " Octopus connected");
      } else if (job && next == count) {
        if (releasesAxes) homed = 0;
        else homed |= homingMask;
        job = false; finishLocal();
      }
    }
  }
#if !TIMIAC_MASTER
  void tick() {
    if (Serial.hasOverrun() && (ready || job)) fail("UART receive buffer overrun");
    unsigned budget = 2048;
    while (Serial.available() && budget--) {
      char c = Serial.read();
      if (c == '\n') {
        if (!serialDiscard) receive(serialLine);
        else if (ready || job) fail("UART line overflow");
        serialLine = ""; serialDiscard = false;
      } else if (c != '\r' && !serialDiscard) {
        if (serialLine.length() < 512) serialLine += c; else serialDiscard = true;
      }
    }
    if (halted) return; // Keep reading for a restart, without polling a killed MCU.
    if (ready && !online()) {
      fail(String(TIMIAC_BOARD_ID == 1 ? "Octopus 1" : "Octopus 2") + " serial response lost");
    }
    if (waiting) {
      if (millis() - sent > (job ? timiac::acknowledgementTimeout(active.c_str()) : 3000)) {
        if (ready || job || staged) fail(timiac::homingCommand(active.c_str()) ? "Octopus homing acknowledgement timed out (180 seconds)" : "Octopus command acknowledgement timed out");
        else { waiting = false; lastPoll = millis(); }
      }
      return;
    }
    if (!ready) {
      if (millis() < 3000 || millis() - lastPoll < 1000) return;
      lastPoll = millis();
      write("M115");
      return;
    }
    if (job && next < count) write(commands[next++]);
    else if (!staged && !job) {
      // Development mode requests live switches via the peer heartbeat.
      // Poll only while idle; never insert telemetry into a staged job or G28.
      const bool monitor = endstopMonitoring && !timiac::expired(millis(), endstopMonitorAt, kPeerTimeout);
      if (millis() - lastPoll >= (monitor ? 250U : 5000U)) {
        lastPoll = millis(); write(monitor ? "M119" : "M115");
      }
    }
  }
#endif
} uart;
