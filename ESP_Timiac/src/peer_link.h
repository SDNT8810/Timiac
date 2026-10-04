#pragma once
String lastBoardBoot[2];
Peer* boardPeer(unsigned board) {
  for (auto& p : peers) if (p.paired && p.board == int(board) && p.socket.connected()) return &p;
  return nullptr;
}
bool boardOnline(unsigned b) {
  return boardPeer(b) && boards[b].online && !timiac::expired(millis(), boards[b].stateAt, kPeerTimeout);
}
bool bothOnline() { return boardOnline(0) && boardOnline(1); }
bool boardStatesHealthy(uint8_t mask) {
  if (!timiac::validBoardMask(mask)) return false;
  for (unsigned b = 0; b < 2; ++b) if (mask & (1U << b))
    if (!boardOnline(b) || !boards[b].drivers.good() || boards[b].latched || !boards[b].fault.isEmpty() ||
        (!stopToken.isEmpty() && boards[b].stopAck != stopToken)) return false;
  return true;
}
bool boardsHealthy(uint8_t mask) {
  return !systemLatched && !stopRequested && !stopPending && !networkChanging && boardStatesHealthy(mask);
}
bool bothHealthy() { return boardsHealthy(3); }
bool workRequiresBoard(unsigned b) {
  return program.reserved() || (operation.barrier.active() && operation.barrier.includes(b));
}
void closePeer(Peer& p, const char* reason) {
  p.closeReason = reason; p.outbox.clear(); p.socket.stop();
}
bool flushPeer(Peer& p) {
  if (!p.socket.connected()) { p.outbox.clear(); return false; }
  if (!p.outbox.pending()) return true;
  if (p.outbox.stalled(millis(), kPeerTimeout)) {
    closePeer(p, "TCP send stalled for 15 seconds"); return false;
  }
  const size_t count = p.outbox.pending() > 1024 ? 1024 : p.outbox.pending();
  const int written = writePeerChunk(p.socket, p.outbox.data(), count);
  if (written < 0) { closePeer(p, "TCP send failed"); return false; }
  p.outbox.advance(written, millis()); return true;
}
bool sendPeer(Peer& p, JsonDocument& doc) {
  if (!flushPeer(p)) return false;
  // Status is refreshed every heartbeat and logs are best effort. Do not let
  // telemetry accumulate behind a blocked socket or crowd out control traffic.
  const bool telemetry = doc["type"] == "state" || doc["type"] == "log" || doc["type"] == "ping";
  if (telemetry && p.outbox.pending()) return true;
  doc["from"] = bootId; doc["session"] = TIMIAC_MASTER ? bootId : coordinatorBoot;
  String packet; serializeJson(doc, packet); packet += '\n';
  if (doc.overflowed() || !p.outbox.append(packet.c_str(), packet.length(), millis())) {
    closePeer(p, "TCP output packet/queue limit exceeded"); return false;
  }
  p.tx = millis(); return flushPeer(p);
}
void logMessage(const String& message) {
  LogEntry& entry = logs[logSequence % 32];
  entry.id = ++logSequence; entry.at = millis(); entry.text = message.substring(0, 180);
  if (!TIMIAC_MASTER && peers[0].paired && !peers[0].outbox.pending()) {
    StaticJsonDocument<512> doc; doc["type"] = "log"; doc["text"] = entry.text; sendPeer(peers[0], doc);
  }
}
void writeBoard(JsonObject board, unsigned index) {
  bool online = TIMIAC_MASTER ? boardOnline(index) : uart.online();
  const auto& remote = boards[index];
  board["online"] = online;
  board["bridgeConnected"] = TIMIAC_MASTER ? boardPeer(index) != nullptr && !timiac::expired(millis(), remote.stateAt, kPeerTimeout) : peers[0].paired;
  board["octopusConnected"] = online;
  board["ready"] = online && (TIMIAC_MASTER ? remote.drivers.good() && remote.fault.isEmpty() && !remote.latched : uart.healthy()) && !systemLatched;
  board["manualReady"] = TIMIAC_MASTER && !TIMIAC_RELEASE && boardsHealthy(1U << index);
  board["busy"] = online && (TIMIAC_MASTER ? remote.busy : uart.job);
  board["fault"] = TIMIAC_MASTER ? remote.fault : (uart.fault.isEmpty() ? systemFault : uart.fault);
  board["resetRequired"] = TIMIAC_MASTER ? remote.resetRequired : uart.halted;
  board["homed"] = online ? (TIMIAC_MASTER ? remote.homed : uart.homed) : 0;
  board["reset"] = TIMIAC_MASTER ? remote.reset : uart.resetCount;
  board["homingMode"] = online ? "switch" : "unknown";
  board["firmware"] = TIMIAC_MASTER ? remote.firmware : (uart.session.identity ? timiac::machineId : "");
  board["software"] = TIMIAC_MASTER ? remote.software.c_str() : kSoftwareRevision;
  board["diagnostic"] = TIMIAC_MASTER ? remote.diagnostic : uart.diagnostic();
  board["bridgeUptimeMs"] = TIMIAC_MASTER ? remote.uptime : millis();
  if (TIMIAC_MASTER) {
    Peer* peer = boardPeer(index);
    board["bridgeId"] = lastBoardBoot[index];
    if (peer) board["bridgeIp"] = peer->socket.remoteIP().toString();
    if (!lastBoardBoot[index].isEmpty()) board["lastSeenMs"] = uint32_t(millis() - remote.stateAt);
    else board["lastSeenMs"] = nullptr;
  }
  const auto& drivers = TIMIAC_MASTER ? remote.drivers : uart.drivers;
  JsonObject checks = board.createNestedObject("drivers");
  checks["tested"] = drivers.tested; checks["connected"] = drivers.connected;
  checks["current"] = drivers.currentOk; checks["microsteps"] = drivers.stepsOk;
  checks["mode"] = drivers.modeOk; checks["columns"] = drivers.columns;
  checks["settings"] = drivers.settings; checks["ok"] = drivers.good();
  // Send the report's values, not just pass/fail masks. A stale browser must
  // never turn a passing bit into an old hard-coded current or resolution.
  JsonArray currents = checks.createNestedArray("currentMa");
  JsonArray microsteps = checks.createNestedArray("microstepValues");
  for (unsigned i = 0; i < 6; ++i) {
    if (drivers.currentMa[i] >= 0) currents.add(drivers.currentMa[i]); else currents.add(nullptr);
    if (drivers.microsteps[i] >= 0) microsteps.add(drivers.microsteps[i]); else microsteps.add(nullptr);
  }
  if (online && (TIMIAC_MASTER ? remote.driverReportAvailable : uart.session.driverReportAvailable))
    checks["ageMs"] = TIMIAC_MASTER ? uint32_t(millis() - remote.driverReportAt) : uart.session.driverReportAge(millis());
  else checks["ageMs"] = nullptr;
  const auto& switches = TIMIAC_MASTER ? remote.endstops : uart.endstops;
  const bool switchData = online && !(TIMIAC_MASTER ? remote.busy : uart.job) && switches.fresh(millis());
  if (switchData) board["endstopsAgeMs"] = uint32_t(millis() - switches.updatedAt);
  else board["endstopsAgeMs"] = nullptr;
  JsonArray endstops = board.createNestedArray("endstops");
  for (unsigned i = 0; i < 6; ++i)
    if (switchData && i < kAxisCount && (switches.known & (1U << i)))
      endstops.add(bool(switches.triggered & (1U << i)));
    else endstops.add(nullptr);
  JsonArray positions = board.createNestedArray("positions");
  for (unsigned i = 0; i < 6; ++i)
    if (i < kAxisCount && online && (TIMIAC_MASTER ? remote.known[i] : uart.known[i]))
      positions.add(TIMIAC_MASTER ? remote.positions[i] : uart.positions[i]);
    else positions.add(nullptr);
}
void sendState() {
  DynamicJsonDocument doc(3072);
  doc["type"] = "state"; writeBoard(doc.createNestedObject("board"), 0);
  doc["id"] = operation.id; doc["phase"] = operation.barrier.name();
  doc["prepared"] = uart.prepared(); doc["error"] = operation.error;
  doc["stopAck"] = acknowledgedStop; doc["latched"] = systemLatched;
  sendPeer(peers[0], doc); lastStateTx = millis();
}
void stopAll(const String& reason) {
  releaseClockMinute = -1;
  releaseStartupPending = false;
  releaseClockEnabled = false;
  if (systemLatched) return;
  // A never-connected, unused board cannot hold recovery hostage. A lost
  // participant must still acknowledge Stop before another job can run.
  if (TIMIAC_MASTER) {
    stopRecoveryRequired = program.reserved() ? 3 : operation.barrier.active() ? operation.barrier.required : 0;
    stopRequired = stopRecoveryRequired;
    for (unsigned b = 0; b < 2; ++b) if (boardPeer(b)) stopRequired |= 1U << b;
  }
  systemLatched = true; systemFault = reason;
  armTimeMinutes = -1;
  if (!TIMIAC_MASTER) uart.abort(reason);
  if (operation.barrier.active()) { operation.barrier.fail(); operation.error = reason; recordReceipt(); }
  if (program.reserved()) {
    program.state = "faulted"; program.error = reason; program.hasStep = false; program.file.close();
    recordProgramReceipt();
  }
  logMessage("FAULT: " + reason);
  if (TIMIAC_MASTER) {
    stopToken = bootId + "-stop-" + String(++sequence); stopReason = reason; stopPending = stopRequired != 0;
    for (auto& p : peers) {
      // Never deliver a queued prepare/commit after cancellation. If any frame
      // has already been partly sent, close instead of truncating its JSON.
      if (p.outbox.partialFrame) closePeer(p, "Stop cancelled a partially sent packet");
      else p.outbox.clear();
      p.tx = 0;
    }
  } else sendState();
}
void recoverStop() {
  if (!TIMIAC_MASTER || !systemLatched) return;
  uint8_t acknowledgements = 0;
  for (unsigned b = 0; b < 2; ++b)
    if (boardPeer(b) && boards[b].stopAck == stopToken) acknowledgements |= 1U << b;
  if (stopPending && timiac::coversBoards(acknowledgements, stopRequired)) {
    stopPending = false; logMessage("Stop delivered to required bridges; checking motor-board state");
  }
  if (!stopPending && !operation.barrier.active() && !program.reserved() &&
      (!stopRecoveryRequired || boardStatesHealthy(stopRecoveryRequired))) {
    systemLatched = false; systemFault = "";
  }
}
void finishLocal() { operation.barrier.done(3); recordReceipt(); sendState(); }
void startOperation(JsonArray a, JsonArray b, const String& id, uint32_t hash, uint8_t mask, bool trajectory) {
  // An accepted manual action also takes over from a pending release startup.
  releaseStartupPending = false;
  operation = Operation(); operation.id = id; operation.hash = hash;
  operation.speedPercent = motionSpeedPercent;
  operation.homing = hasHoming(a) || hasHoming(b);
  operation.barrier.begin(millis(), mask); recordReceipt();
  if (trajectory && (!boards[0].trajectorySupported || !boards[1].trajectorySupported ||
      !validLines(a, true) || !validLines(b, true))) {
    stopAll("Continuous motion requires updated ESP bridges and valid trajectory commands"); return;
  }
  for (unsigned i = 0; i < 2; ++i) {
    if (!operation.barrier.includes(i)) continue;
    Peer* p = boardPeer(i);
    DynamicJsonDocument doc(trajectory ? 4096 : 2304);
    doc["type"] = "prepare"; doc["id"] = id; doc["hash"] = hash; doc["lines"] = i == 0 ? a : b;
    doc["homing"] = operation.homing;
    doc["speedPercent"] = operation.speedPercent;
    if (trajectory) doc["trajectory"] = true;
    if (!p || !sendPeer(*p, doc)) { stopAll("Could not prepare required board bridge"); return; }
  }
}
void processPeer(Peer& p, const String& line) {
  DynamicJsonDocument doc(4096);
  const DeserializationError parseError = deserializeJson(doc, line);
  if (parseError) {
    if (TIMIAC_MASTER) logMessage("Bridge packet rejected: " + String(parseError.c_str()));
    p.socket.stop(); return;
  }
  String type = doc["type"] | "", from = doc["from"] | "";
  if (!p.paired) {
    if (TIMIAC_MASTER && type == "hello" && doc["key"] == TIMIAC_PEER_KEY && !from.isEmpty()) {
      int id = doc["board"] | 0;
      if (id < 1 || id > 2) { p.socket.stop(); return; }
      if (Peer* existing = boardPeer(id - 1)) {
        if (millis() - lastPeerRejection >= 5000) {
          lastPeerRejection = millis();
          logMessage("Board " + String(id) + " duplicate role rejected: ESP " + from + "; connected ESP " + existing->boot);
        }
        p.socket.stop(); return;
      }
      if (doc["axes"] != kAxisCount || doc["version"] != timiac::protocolVersion || doc["machine"] != timiac::machineId) {
        boards[id - 1].fault = "ESP firmware mismatch: flash this bridge with the matching TIMIAC2 release";
        boards[id - 1].online = false; p.socket.stop(); return;
      }
      p.board = id - 1;
      const bool sameBoot = from == lastBoardBoot[p.board];
      if (!lastBoardBoot[p.board].isEmpty() && !sameBoot &&
          workRequiresBoard(p.board)) stopAll("A required board bridge restarted; work stopped");
      lastBoardBoot[p.board] = from;
      boards[p.board].software = doc["software"] | "unknown";
      boards[p.board].trajectorySupported = doc["trajectoryVersion"] == timiac::trajectoryVersion;
      boards[p.board].online = false;
      boards[p.board].stopAck = "";
      boards[p.board].driverReportAvailable = false;
      boards[p.board].endstops.clear();
      p.boot = from; p.paired = true;
      StaticJsonDocument<384> reply; reply["type"] = "welcome"; reply["version"] = timiac::protocolVersion;
      reply["machine"] = timiac::machineId; sendPeer(p, reply);
      logMessage("Board " + String(id) + " ESP " + from + " at " + p.socket.remoteIP().toString() +
        (sameBoot ? " reconnected (same ESP boot); software " : " connected; software ") + boards[p.board].software);
      if (!sameBoot && doc["resetReason"].is<const char*>())
        logMessage("Board " + String(id) + " boot reason: " + String(doc["resetReason"].as<const char*>()));
    } else if (!TIMIAC_MASTER && type == "welcome" && doc["version"] == timiac::protocolVersion && doc["machine"] == timiac::machineId && !from.isEmpty()) {
      if (!coordinatorBoot.isEmpty() && coordinatorBoot != from && operation.barrier.active())
        stopAll("Master restarted during command; position is uncertain");
      coordinatorBoot = from; p.boot = from; p.paired = true; sendState();
    } else p.socket.stop();
    p.rx = millis(); reconnectDelay = 500; return;
  }
  if (from != p.boot || doc["session"] != (TIMIAC_MASTER ? bootId : coordinatorBoot)) { p.socket.stop(); return; }
  p.rx = millis();
  if (type == "ping") {
    if (!TIMIAC_MASTER) uart.monitorEndstops(doc["monitorEndstops"] | false);
    return;
  }
  if (TIMIAC_MASTER && type == "log") { logMessage("Board " + String(p.board + 1) + ": " + String(doc["text"] | "")); return; }
  if (TIMIAC_MASTER && type == "state") {
    auto& board = boards[p.board];
    uint32_t reset = doc["board"]["reset"] | 0U;
    if (reset != board.reset && workRequiresBoard(p.board))
      stopAll("An Octopus restarted; both boards stopped");
    board.reset = reset; board.stateAt = millis();
    board.online = doc["board"]["online"] | false; board.busy = doc["board"]["busy"] | false;
    board.homed = doc["board"]["homed"] | 0; board.fault = doc["board"]["fault"].as<String>();
    board.latched = doc["latched"] | false; board.stopAck = doc["stopAck"].as<String>();
    board.resetRequired = doc["board"]["resetRequired"] | false;
    board.firmware = doc["board"]["firmware"] | "";
    board.diagnostic = doc["board"]["diagnostic"] | "";
    board.uptime = doc["board"]["bridgeUptimeMs"] | 0U;
    const JsonObject checks = doc["board"]["drivers"];
    board.driverReportAvailable = checks["ageMs"].is<uint32_t>();
    if (board.driverReportAvailable) board.driverReportAt = millis() - checks["ageMs"].as<uint32_t>();
    board.drivers.tested = checks["tested"] | 0;
    board.drivers.connected = checks["connected"] | 0;
    board.drivers.currentOk = checks["current"] | 0;
    board.drivers.stepsOk = checks["microsteps"] | 0;
    board.drivers.modeOk = checks["mode"] | 0;
    board.drivers.columns = checks["columns"] | false;
    board.drivers.settings = checks["settings"] | 0;
    for (unsigned i = 0; i < 6; ++i) {
      board.drivers.currentMa[i] = checks["currentMa"][i].is<uint16_t>() ? checks["currentMa"][i].as<int32_t>() : -1;
      board.drivers.microsteps[i] = checks["microstepValues"][i].is<uint16_t>() ? checks["microstepValues"][i].as<int32_t>() : -1;
    }
    if (board.latched && board.fault.isEmpty()) {
      board.fault = doc["error"] | "";
      if (board.fault.isEmpty()) board.fault = "Bridge fault; check the board log";
    }
    board.endstops.clear();
    const uint32_t switchAge = doc["board"]["endstopsAgeMs"] | timiac::kEndstopFreshMs;
    board.endstops.updatedAt = millis() - switchAge;
    for (unsigned i = 0; i < 6; ++i) {
      if (switchAge < timiac::kEndstopFreshMs && doc["board"]["endstops"][i].is<bool>()) {
        board.endstops.known |= 1U << i;
        if (doc["board"]["endstops"][i].as<bool>()) board.endstops.triggered |= 1U << i;
      }
      board.known[i] = doc["board"]["positions"][i].is<float>();
      board.positions[i] = doc["board"]["positions"][i] | 0.0f;
    }
    if ((!board.fault.isEmpty() || board.latched) && !systemLatched && workRequiresBoard(p.board))
      stopAll("Board " + String(p.board + 1) + ": " + board.fault);
    recoverStop();
    if (operation.barrier.active() && operation.barrier.includes(p.board) && doc["id"] == operation.id) {
      String phase = doc["phase"] | "";
      if (phase == "failed") stopAll("Board operation failed: " + doc["error"].as<String>());
      else if (operation.barrier.phase == timiac::Phase::Preparing && phase == "preparing" && doc["prepared"] == true)
        operation.barrier.prepared |= 1U << p.board;
      else if (phase == "done") { operation.barrier.done(1U << p.board); recordReceipt(); }
    }
  } else if (!TIMIAC_MASTER && type == "stop") {
    String token = doc["token"] | "";
    if (token.isEmpty()) { p.socket.stop(); return; }
    if (token != acknowledgedStop) uart.abort(doc["reason"] | "Master stop");
    // Cancel the barrier as well as the UART queue. Otherwise the old homing
    // deadline keeps firing after Stop. A local hardware fault survives Stop.
    acknowledgedStop = token;
    systemLatched = !uart.fault.isEmpty(); systemFault = uart.fault;
    sendState();
  } else if (!TIMIAC_MASTER && type == "prepare") {
    String id = doc["id"] | "";
    const bool trajectory = doc["trajectory"] | false;
    const unsigned speedPercent = doc["speedPercent"] | 0U;
    if (id == operation.id) {
      if (doc["hash"] != operation.hash || speedPercent != operation.speedPercent) stopAll("Conflicting duplicate preparation");
      sendState(); return;
    }
    bool stale = false; for (auto& r : receipts) if (r.id == id) stale = true;
    if (stale || id.isEmpty() || id.length() > 96 || !doc["speedPercent"].is<unsigned>() || !timiac::validSpeedPercent(speedPercent) || !validLines(doc["lines"], trajectory) || !uart.healthy() ||
        systemLatched || operation.barrier.active()) {
      stopAll("Preparation rejected: unavailable board, unhomed axis, stale or invalid command"); return;
    }
    operation = Operation(); operation.id = id; operation.hash = doc["hash"] | 0U;
    operation.speedPercent = speedPercent;
    // The idle peer also waits through the other board's homing operation.
    operation.homing = (doc["homing"] | false) || hasHoming(doc["lines"].as<JsonArray>());
    operation.barrier.begin(millis()); uart.stage(doc["lines"].as<JsonArray>(), trajectory, speedPercent); recordReceipt(); sendState();
  } else if (!TIMIAC_MASTER && type == "commit") {
    if (doc["id"] != operation.id) { stopAll("Commit does not match prepared command"); return; }
    if (operation.barrier.phase == timiac::Phase::Running || operation.barrier.phase == timiac::Phase::Done) { sendState(); return; }
    if (!uart.prepared() || systemLatched || operation.barrier.phase != timiac::Phase::Preparing) {
      stopAll("Commit refused: board readiness changed"); return;
    }
    operation.barrier.phase = timiac::Phase::Running; operation.barrier.since = millis(); uart.commit(); sendState();
  }
}
void networkTick() {
  for (auto& p : peers) {
    flushPeer(p);
    if ((p.paired || p.socket.connected()) && (!p.socket.connected() || timiac::expired(millis(), p.rx, kPeerTimeout))) {
      bool wasPaired = p.paired;
      if (TIMIAC_MASTER && p.board >= 0) boards[p.board].online = false;
      p.paired = false; p.outbox.clear(); p.socket.stop();
      if (wasPaired) {
        const String reason = !p.closeReason.isEmpty() ? p.closeReason :
          timiac::expired(millis(), p.rx, kPeerTimeout) ? "no peer packet for 15 seconds" : "TCP connection closed";
        logMessage((TIMIAC_MASTER ? "Board " + String(p.board + 1) : String("Master")) + " link lost: " + reason + "; ESP " + p.boot);
        // A connection cannot authorize continued coordinated motion after
        // its 15-second deadline. Reconnect for status; never replay a move.
        if (TIMIAC_MASTER ? workRequiresBoard(p.board) : operation.barrier.active() || uart.job)
          stopAll("Bridge link lost during work; movement cancelled, no replay");
      }
    }
  }
  if (TIMIAC_MASTER && listener.hasClient()) {
    Peer* free = nullptr; for (auto& p : peers) if (!p.socket.connected()) { free = &p; break; }
    WiFiClient incoming = listener.accept();
    if (!free) incoming.stop();
    else {
      free->socket = incoming; free->socket.setNoDelay(true);
      free->outbox.clear(); free->closeReason = "";
#if defined(ARDUINO_ARCH_ESP8266)
      free->socket.setTimeout(20); // ESP8266 milliseconds; ESP32 uses seconds.
#endif
      free->paired = false; free->board = -1; free->rx = millis(); free->line = ""; free->discard = false;
    }
  }
  if (!TIMIAC_MASTER) {
    bool wifi = WiFi.status() == WL_CONNECTED;
    if (!wifi && millis() - lastWifiRetry > 10000) { lastWifiRetry = millis(); WiFi.reconnect(); }
    Peer& p = peers[0];
    if (!p.socket.connected() && wifi && millis() - reconnectAt > reconnectDelay) {
      reconnectAt = millis(); reconnectDelay = reconnectDelay * 2 + (platformRandom() % 200);
      if (reconnectDelay > 5000) reconnectDelay = 5000;
      p.socket.setTimeout(50);
      if (p.socket.connect(IPAddress(192,168,4,1), kPeerPort)) {
        p.outbox.clear(); p.closeReason = "";
        p.paired = false; p.rx = millis(); p.line = ""; p.discard = false;
        p.socket.setTimeout(20); p.socket.setNoDelay(true);
        StaticJsonDocument<768> hello;
        hello["type"] = "hello"; hello["key"] = TIMIAC_PEER_KEY; hello["version"] = timiac::protocolVersion;
        hello["machine"] = timiac::machineId;
        hello["software"] = kSoftwareRevision; hello["resetReason"] = bridgeResetReason;
        hello["trajectoryVersion"] = timiac::trajectoryVersion;
        hello["board"] = TIMIAC_BOARD_ID; hello["axes"] = kAxisCount; sendPeer(p, hello);
      }
    }
  }
  for (auto& p : peers) {
    unsigned budget = 3072;
    while (p.socket.available() && budget--) {
      char c = p.socket.read();
      if (c == '\n') {
        if (!p.discard) processPeer(p, p.line); else closePeer(p, "TCP input packet too long");
        p.line = ""; p.discard = false;
      } else if (c != '\r' && !p.discard) {
        if (p.line.length() < 2600) p.line += c; else p.discard = true;
      }
    }
    if (!p.paired) continue;
    if (TIMIAC_MASTER && !p.outbox.pending() && millis() - p.tx >= kHeartbeatMs) {
      StaticJsonDocument<512> doc;
      // Fence a late/reconnected board with the latest Stop even after the
      // original participants have acknowledged. Never replay interrupted work.
      if (!stopToken.isEmpty() && boards[p.board].stopAck != stopToken) {
        doc["type"] = "stop"; doc["token"] = stopToken; doc["reason"] = stopReason;
      }
      else { doc["type"] = "ping"; doc["monitorEndstops"] = !TIMIAC_RELEASE; }
      sendPeer(p, doc);
    }
  }
  recoverStop();
  if (!TIMIAC_MASTER && peers[0].paired && millis() - lastStateTx >= kHeartbeatMs) sendState();
}
void operationTick() {
  if (stopRequested) {
    stopRequested = false;
    stopAll("Operator stop");
  }
  if (!operation.barrier.active()) return;
  // UART homing gets 180 seconds; ESP heartbeats remain independent of G28.
  // Preparation and completion wait only for this operation's participants.
  if (TIMIAC_MASTER && operation.barrier.phase == timiac::Phase::Running && !boardsHealthy(operation.barrier.required)) {
    stopAll("Required-board health check failed during command"); return;
  }
  if (operation.barrier.phase == timiac::Phase::Preparing) {
    if (TIMIAC_MASTER && operation.barrier.canStart(boardsHealthy(operation.barrier.required))) {
      operation.barrier.phase = timiac::Phase::Running;
      operation.barrier.since = millis();
      for (unsigned b = 0; b < 2; ++b) {
        if (!operation.barrier.includes(b)) continue;
        Peer* p = boardPeer(b); StaticJsonDocument<384> doc; doc["type"] = "commit"; doc["id"] = operation.id;
        if (!p || !sendPeer(*p, doc)) { stopAll("Commit delivery failed"); return; }
      }
      recordReceipt();
    } else if (millis() - operation.barrier.since > kPrepareTimeout) stopAll("Required-board preparation timed out");
  } else if (millis() - operation.barrier.since > timiac::operationTimeout(operation.homing, operation.speedPercent))
    stopAll(operation.homing ? "Homing completion timed out (180 seconds)" : "Motion completion timed out");
}
