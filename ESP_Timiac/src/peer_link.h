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
bool bothHealthy() {
  return bothOnline() && boards[0].drivers.good() && boards[1].drivers.good() && !systemLatched && !stopRequested && !stopPending && !networkChanging &&
    !boards[0].latched && !boards[1].latched && boards[0].fault.isEmpty() && boards[1].fault.isEmpty();
}
bool sendPeer(Peer& p, JsonDocument& doc) {
  if (!p.socket.connected()) return false;
  doc["from"] = bootId; doc["session"] = TIMIAC_MASTER ? bootId : coordinatorBoot;
  String packet; serializeJson(doc, packet); packet += '\n';
  if (!writePeerPacket(p.socket, packet)) {
    p.socket.stop(); return false;
  }
  p.tx = millis(); return true;
}
void logMessage(const String& message) {
  LogEntry& entry = logs[logSequence % 32];
  entry.id = ++logSequence; entry.at = millis(); entry.text = message.substring(0, 180);
  if (!TIMIAC_MASTER && peers[0].paired && peers[0].socket.availableForWrite() > 600) {
    StaticJsonDocument<512> doc; doc["type"] = "log"; doc["text"] = entry.text; sendPeer(peers[0], doc);
  }
}
void writeBoard(JsonObject board, unsigned index) {
  bool online = TIMIAC_MASTER ? boardOnline(index) : uart.online();
  const auto& remote = boards[index];
  board["online"] = online;
  board["bridgeConnected"] = TIMIAC_MASTER ? boardPeer(index) != nullptr && !timiac::expired(millis(), remote.stateAt, kPeerTimeout) : peers[0].paired;
  board["octopusConnected"] = online;
  board["ready"] = online && (TIMIAC_MASTER ? remote.fault.isEmpty() && !remote.latched : uart.fault.isEmpty()) && !systemLatched;
  board["busy"] = online && (TIMIAC_MASTER ? remote.busy : uart.job);
  board["fault"] = TIMIAC_MASTER ? remote.fault : (uart.fault.isEmpty() ? systemFault : uart.fault);
  board["resetRequired"] = TIMIAC_MASTER ? remote.resetRequired : uart.halted;
  board["homed"] = online ? (TIMIAC_MASTER ? remote.homed : uart.homed) : 0;
  board["reset"] = TIMIAC_MASTER ? remote.reset : uart.resetCount;
  board["firmware"] = TIMIAC_MASTER ? remote.firmware : (uart.session.identity ? timiac::machineId : "");
  const auto& drivers = TIMIAC_MASTER ? remote.drivers : uart.drivers;
  JsonObject checks = board.createNestedObject("drivers");
  checks["tested"] = drivers.tested; checks["connected"] = drivers.connected;
  checks["current"] = drivers.currentOk; checks["microsteps"] = drivers.stepsOk;
  checks["mode"] = drivers.modeOk; checks["columns"] = drivers.columns;
  checks["settings"] = drivers.settings; checks["ok"] = drivers.good();
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
  DynamicJsonDocument doc(2048);
  doc["type"] = "state"; writeBoard(doc.createNestedObject("board"), 0);
  doc["id"] = operation.id; doc["phase"] = operation.barrier.name();
  doc["prepared"] = uart.prepared(); doc["error"] = operation.error;
  doc["stopAck"] = acknowledgedStop; doc["latched"] = systemLatched;
  sendPeer(peers[0], doc); lastStateTx = millis();
}
void stopAll(const String& reason) {
  if (systemLatched) return;
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
    stopToken = bootId + "-stop-" + String(++sequence); stopPending = true;
    for (auto& p : peers) p.tx = 0;
  } else sendState();
}
void finishLocal() { operation.barrier.done(3); recordReceipt(); sendState(); }
void startOperation(JsonArray a, JsonArray b, const String& id, uint32_t hash) {
  operation = Operation(); operation.id = id; operation.hash = hash;
  operation.homing = hasHoming(a) || hasHoming(b);
  operation.barrier.begin(millis()); recordReceipt();
  for (unsigned i = 0; i < 2; ++i) {
    Peer* p = boardPeer(i);
    DynamicJsonDocument doc(2304);
    doc["type"] = "prepare"; doc["id"] = id; doc["hash"] = hash; doc["lines"] = i == 0 ? a : b;
    doc["homing"] = operation.homing;
    if (!p || !sendPeer(*p, doc)) { stopAll("Could not prepare both board bridges"); return; }
  }
}
void processPeer(Peer& p, const String& line) {
  DynamicJsonDocument doc(3072);
  if (deserializeJson(doc, line)) { p.socket.stop(); return; }
  String type = doc["type"] | "", from = doc["from"] | "";
  if (!p.paired) {
    if (TIMIAC_MASTER && type == "hello" && doc["key"] == TIMIAC_PEER_KEY && !from.isEmpty()) {
      int id = doc["board"] | 0;
      if (id < 1 || id > 2 || boardPeer(id - 1)) { p.socket.stop(); return; }
      if (doc["axes"] != kAxisCount || doc["version"] != timiac::protocolVersion || doc["machine"] != timiac::machineId) {
        boards[id - 1].fault = "ESP firmware mismatch: flash this bridge with the matching TIMIAC2 release";
        boards[id - 1].online = false; p.socket.stop(); return;
      }
      p.board = id - 1;
      if (!lastBoardBoot[p.board].isEmpty() && from != lastBoardBoot[p.board] &&
          (operation.barrier.active() || program.reserved())) stopAll("A board bridge restarted; program stopped");
      lastBoardBoot[p.board] = from;
      boards[p.board].online = false;
      boards[p.board].endstops.clear();
      p.boot = from; p.paired = true;
      StaticJsonDocument<384> reply; reply["type"] = "welcome"; reply["version"] = timiac::protocolVersion;
      reply["machine"] = timiac::machineId; sendPeer(p, reply);
      logMessage("Board " + String(id) + " bridge connected; waiting for Marlin");
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
    if (reset != board.reset && (operation.barrier.active() || program.state == "running" || program.state == "paused"))
      stopAll("An Octopus restarted; both boards stopped");
    board.reset = reset; board.stateAt = millis();
    board.online = doc["board"]["online"] | false; board.busy = doc["board"]["busy"] | false;
    board.homed = doc["board"]["homed"] | 0; board.fault = doc["board"]["fault"].as<String>();
    board.latched = doc["latched"] | false; board.stopAck = doc["stopAck"].as<String>();
    board.resetRequired = doc["board"]["resetRequired"] | false;
    board.firmware = doc["board"]["firmware"] | "";
    const JsonObject checks = doc["board"]["drivers"];
    board.drivers.tested = checks["tested"] | 0;
    board.drivers.connected = checks["connected"] | 0;
    board.drivers.currentOk = checks["current"] | 0;
    board.drivers.stepsOk = checks["microsteps"] | 0;
    board.drivers.modeOk = checks["mode"] | 0;
    board.drivers.columns = checks["columns"] | false;
    board.drivers.settings = checks["settings"] | 0;
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
    if (stopPending && boards[0].stopAck == stopToken && boards[1].stopAck == stopToken) {
      stopPending = false; logMessage("Stop delivered to both bridges; checking motor-board state");
    }
    if ((!board.fault.isEmpty() || board.latched) && !systemLatched && (operation.barrier.active() || program.reserved()))
      stopAll("Board " + String(p.board + 1) + ": " + board.fault);
    // Stop cancels bridge-side queued work only. Once both bridges report a
    // clear state, Home is available again without a reboot or recovery step.
    if (systemLatched && !operation.barrier.active() && !program.reserved() &&
        boards[0].online && boards[1].online && !boards[0].latched && !boards[1].latched &&
        boards[0].fault.isEmpty() && boards[1].fault.isEmpty() && boards[0].drivers.good() && boards[1].drivers.good()) {
      systemLatched = false; systemFault = "";
    }
    if (operation.barrier.active() && doc["id"] == operation.id) {
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
    if (id == operation.id) {
      if (doc["hash"] != operation.hash) stopAll("Conflicting duplicate preparation");
      sendState(); return;
    }
    bool stale = false; for (auto& r : receipts) if (r.id == id) stale = true;
    if (stale || id.isEmpty() || id.length() > 96 || !validLines(doc["lines"]) || !uart.healthy() ||
        systemLatched || operation.barrier.active()) {
      stopAll("Preparation rejected: unavailable board, unhomed axis, stale or invalid command"); return;
    }
    operation = Operation(); operation.id = id; operation.hash = doc["hash"] | 0U;
    // The idle peer also waits through the other board's homing operation.
    operation.homing = (doc["homing"] | false) || hasHoming(doc["lines"].as<JsonArray>());
    operation.barrier.begin(millis()); uart.stage(doc["lines"].as<JsonArray>()); recordReceipt(); sendState();
  } else if (!TIMIAC_MASTER && type == "commit") {
    if (doc["id"] != operation.id) { stopAll("Commit does not match prepared command"); return; }
    if (operation.barrier.phase == timiac::Phase::Running || operation.barrier.phase == timiac::Phase::Done) { sendState(); return; }
    if (!uart.prepared() || systemLatched || operation.barrier.phase != timiac::Phase::Preparing) {
      stopAll("Commit refused: board readiness changed"); return;
    }
    operation.barrier.phase = timiac::Phase::Running; uart.commit(); sendState();
  }
}
void networkTick() {
  for (auto& p : peers) {
    if ((p.paired || p.socket.connected()) && (!p.socket.connected() || timiac::expired(millis(), p.rx, kPeerTimeout))) {
      bool wasPaired = p.paired;
      if (TIMIAC_MASTER && p.board >= 0) boards[p.board].online = false;
      p.paired = false; p.socket.stop();
      if (wasPaired) {
        logMessage("Bridge link lost; reconnecting");
        // A homing command is driven locally by Marlin endstops. Continue it
        // across a transient Wi-Fi reconnection without injecting Marlin
        // emergency, reset, or recovery commands.
        if (operation.barrier.active() || program.state == "running" || program.state == "paused" ||
            (!TIMIAC_MASTER && uart.job)) stopAll("Bridge link lost during work; movement cancelled, no replay");
      }
    }
  }
  if (TIMIAC_MASTER && listener.hasClient()) {
    Peer* free = nullptr; for (auto& p : peers) if (!p.socket.connected()) { free = &p; break; }
    WiFiClient incoming = listener.accept();
    if (!free) incoming.stop();
    else {
      free->socket = incoming; free->socket.setNoDelay(true);
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
        p.paired = false; p.rx = millis(); p.line = ""; p.discard = false;
        p.socket.setTimeout(20); p.socket.setNoDelay(true);
        StaticJsonDocument<512> hello;
        hello["type"] = "hello"; hello["key"] = TIMIAC_PEER_KEY; hello["version"] = timiac::protocolVersion;
        hello["machine"] = timiac::machineId;
        hello["board"] = TIMIAC_BOARD_ID; hello["axes"] = kAxisCount; sendPeer(p, hello);
      }
    }
  }
  for (auto& p : peers) {
    unsigned budget = 3072;
    while (p.socket.available() && budget--) {
      char c = p.socket.read();
      if (c == '\n') {
        if (!p.discard) processPeer(p, p.line); else p.socket.stop();
        p.line = ""; p.discard = false;
      } else if (c != '\r' && !p.discard) {
        if (p.line.length() < 2600) p.line += c; else p.discard = true;
      }
    }
    if (!p.paired) continue;
    if (TIMIAC_MASTER && millis() - p.tx >= kHeartbeatMs) {
      StaticJsonDocument<512> doc;
      if (stopPending) { doc["type"] = "stop"; doc["token"] = stopToken; doc["reason"] = systemFault; }
      else { doc["type"] = "ping"; doc["monitorEndstops"] = !TIMIAC_RELEASE; }
      sendPeer(p, doc);
    }
  }
  if (!TIMIAC_MASTER && peers[0].paired && millis() - lastStateTx >= kHeartbeatMs) sendState();
}
void operationTick() {
  if (stopRequested) {
    stopRequested = false;
    stopAll("Operator stop");
  }
  if (!operation.barrier.active()) return;
  // A G28 is a self-contained endstop move. Do not turn a momentary Wi-Fi or
  // status-packet gap into a motor-board command while it is still running. Each bridge gives
  // its active G28 180 seconds, the master has the same operation deadline,
  // and an explicit board fault is still propagated by processPeer() above.
  if (TIMIAC_MASTER && !bothHealthy()) {
    stopAll("Both-board health check failed during command"); return;
  }
  if (operation.barrier.phase == timiac::Phase::Preparing) {
    if (TIMIAC_MASTER && operation.barrier.canStart(bothHealthy())) {
      operation.barrier.phase = timiac::Phase::Running;
      operation.barrier.since = millis();
      for (unsigned b = 0; b < 2; ++b) {
        Peer* p = boardPeer(b); StaticJsonDocument<384> doc; doc["type"] = "commit"; doc["id"] = operation.id;
        if (!p || !sendPeer(*p, doc)) { stopAll("Commit delivery failed"); return; }
      }
      recordReceipt();
    } else if (millis() - operation.barrier.since > kPrepareTimeout) stopAll("Both-board preparation timed out");
  } else if (millis() - operation.barrier.since > timiac::operationTimeout(operation.homing))
    stopAll(operation.homing ? "Homing completion timed out (180 seconds)" : "Motion completion timed out");
}
