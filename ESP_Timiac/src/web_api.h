#pragma once
uint32_t snapshotSerial = 0;

void errorReply(AsyncWebServerRequest* request, unsigned code, const String& message) {
  StaticJsonDocument<384> doc; doc["error"] = message;
  String text; serializeJson(doc, text); request->send(code, "application/json", text);
}
void statusReply(AsyncWebServerRequest* request) {
  RuntimeLock lock;
  if (platformFreeNetworkHeap() < (cloudInFlight ? 32768U : 24000U) || platformLargestNetworkBlock() < 10000) { errorReply(request, 503, "Controller busy; retry status shortly"); return; }
  DynamicJsonDocument doc(8192);
  doc["mode"] = "hardware"; doc["session"] = bootId; doc["revision"] = ++snapshotSerial;
  doc["dashboard"] = TIMIAC_RELEASE ? "release" : "development";
  doc["firmware"] = timiac::machineId; doc["protocol"] = timiac::protocolVersion;
  doc["software"] = kSoftwareRevision;
  writeReliabilityStatus(doc.createNestedObject("reliability"));
  writeFirmwareUpdateStatus(doc.createNestedObject("ota"));
  doc["speedPercent"] = motionSpeedPercent;
  JsonObject clock = doc.createNestedObject("clock");
  writeRtcStatus(clock);
  writeNtpStatus(clock);
  clock["armMinute"] = armTimeMinutes;
  clock["automatic"] = bool(TIMIAC_RELEASE);
  clock["running"] = releaseClockEnabled;
  clock["startupPending"] = releaseStartupPending;
  clock["pausedReason"] = releaseClockEnabled ? "" : clockPauseReason;
  writeProximityStatus(doc.createNestedObject("proximity"));
  writeUplinkStatus(doc.createNestedObject("wifi"));
  writeCloudStatus(doc.createNestedObject("cloud"));
  doc["peerConnected"] = boardPeer(0) && boardPeer(1); doc["axesPerBoard"] = kAxisCount; doc["stopPending"] = stopPending || stopRequested;
  doc["travelMm"] = kTravelMm; doc["maxFeed"] = kMaxFeed; doc["stations"] = WiFi.softAPgetStationNum();
  doc["machineReady"] = bothHealthy(); doc["fault"] = systemFault;
  doc["machineState"] = systemLatched ? "faulted" : bothHealthy() ? "ready" : "reconnecting";
  writeBoard(doc.createNestedObject("a"), 0); writeBoard(doc.createNestedObject("b"), 1);
  JsonObject op = doc.createNestedObject("operation");
  op["id"] = operation.id; op["state"] = operation.barrier.name(); op["error"] = operation.error;
  op["homing"] = operation.homing; op["timeoutMs"] = timiac::operationTimeout(operation.homing, operation.speedPercent);
  op["boardMask"] = operation.barrier.required;
  if (request->hasParam("operation")) {
    String id = request->getParam("operation")->value();
    JsonObject result = doc.createNestedObject("result"); result["id"] = id; result["state"] = "unknown";
    for (auto& receipt : receipts) if (receipt.id == id) {
      result["state"] = receipt.state; result["error"] = receipt.error; break;
    }
    if (program.builtin && program.id == id) {
      result["state"] = program.state == "faulted" ? "failed" : program.state;
      result["error"] = program.error;
    }
  }
  JsonObject owner = doc.createNestedObject("control");
  owner["owner"] = lease.live() ? lease.owner : ""; owner["label"] = lease.live() ? lease.label : "";
  owner["fence"] = lease.fence; owner["expiresInMs"] = lease.live() ? kLeaseMs - (millis() - lease.renewed) : 0;
  JsonObject job = doc.createNestedObject("program");
  job["id"] = program.id; job["name"] = program.name; job["state"] = program.state;
  job["total"] = program.total; job["uploaded"] = program.uploaded; job["completed"] = program.completed;
  job["error"] = program.error; job["translation"] = program.translation;
  job["builtin"] = program.builtin;
#if !TIMIAC_RELEASE
  JsonArray history = doc.createNestedArray("events");
  uint32_t after = request->hasParam("after") ? strtoul(request->getParam("after")->value().c_str(), nullptr, 10) : 0;
  uint32_t first = logSequence > 8 ? logSequence - 8 : 0;
  for (uint32_t i = first; i < logSequence; ++i) {
    const LogEntry& entry = logs[i % 32]; if (entry.id <= after) continue;
    JsonObject event = history.createNestedObject(); event["id"] = entry.id; event["at"] = entry.at; event["text"] = entry.text;
  }
  doc["logSequence"] = logSequence;
#endif
  String text; serializeJson(doc, text);
  AsyncWebServerResponse* response = request->beginResponse(200, "application/json", text);
  response->addHeader("Cache-Control", "no-store"); response->addHeader("Connection", "close"); request->send(response);
}
bool authorized(AsyncWebServerRequest* request, JsonVariant doc, bool writer) {
  if (!request->hasHeader("X-Timiac") || request->getHeader("X-Timiac")->value() != "1") {
    errorReply(request, 403, "Missing control header"); return false;
  }
  if (doc["session"] != bootId) { errorReply(request, 409, "Controller restarted; synchronize status first"); return false; }
  if (writer && (!lease.live() || doc["client"] != lease.owner || doc["fence"] != lease.fence)) {
    errorReply(request, 423, "Another phone has control, or your control lease expired"); return false;
  }
  if (writer) lease.renewed = millis();
  return true;
}
void handleApi(AsyncWebServerRequest* request, JsonVariant doc) {
  RuntimeLock lock;
  String path = request->url();
  if (path == "/api/stop") {
    if (!request->hasHeader("X-Timiac") || request->getHeader("X-Timiac")->value() != "1") {
      errorReply(request, 403, "Missing control header"); return;
    }
    stopRequested = true; statusReply(request); return;
  }
  if (firmwareUpdating) { errorReply(request, 409, "Firmware update in progress; only Stop is available"); return; }
  bool writer = path != "/api/claim";
  if (!authorized(request, doc, writer)) return;
  if (path == "/api/speed") {
    const char* error = saveMotionSpeed(doc);
    if (error) { errorReply(request, 409, error); return; }
    statusReply(request); return;
  }
  if (path == "/api/wifi") {
    const char* error = saveUplink(doc);
    if (error) { errorReply(request, 400, error); return; }
    statusReply(request); return;
  }
  if (path == "/api/time") {
    DateTime date;
    if (!doc["localTime"].is<const char*>() || !parseRtcDate(doc["localTime"].as<String>(), date)) {
      errorReply(request, 400, "Enter a valid local date and time from 2000 to 2099"); return;
    }
    if (!setRtcDate(date)) { errorReply(request, 503, rtcError.isEmpty() ? "RTC write could not be verified" : rtcError); return; }
    armTimeMinutes = -1;
    statusReply(request); return;
  }
  if (path == "/api/action") {
    String action = doc["action"] | "";
#if TIMIAC_RELEASE
    if (action != "home" && action != "open" && action != "close" && action != "show-time" && action != "next-five" &&
        action != "next-hour" && action != "struggle") {
      errorReply(request, 404, "Development actions are unavailable in release mode"); return;
    }
#endif
    bool animation = action == "struggle" || action == "next-hour";
    bool nextTime = action == "next-five" || action == "next-hour";
    if (action != "home" && action != "open" && action != "close" && action != "show-time" && action != "next-five" && !animation) {
      errorReply(request, 400, "Unknown user action"); return;
    }
    String id = doc["id"] | "";
    uint32_t actionHash = hashText("action:" + action);
    if (!id.isEmpty() && program.builtin && program.id == id) {
      if (program.actionHash != actionHash) { errorReply(request, 409, "Request ID reused with another action"); return; }
      statusReply(request); return;
    }
    // Resolve retries before reading the clock again: an accepted pose is immutable.
    for (auto& r : receipts) if (!id.isEmpty() && r.id == id) {
      if (r.hash != actionHash) { errorReply(request, 409, "Request ID reused with another action"); return; }
      statusReply(request); return;
    }
    unsigned targetMinute = 0;
    if (action == "show-time" || action == "struggle" || nextTime) {
      rtcTick(true);
      if ((action == "show-time" || action == "struggle" || armTimeMinutes < 0) && !rtcValid) { errorReply(request, 503, rtcError); return; }
      targetMinute = nextTime && armTimeMinutes >= 0 ? armTimeMinutes : rtcTime.hour() * 60 + rtcTime.minute();
      if (nextTime) targetMinute = timiac::nextClockMinute(targetMinute, action == "next-five");
    }
    // Validate ownership, sequence and readiness before starting any preset.
    if (id.isEmpty() || id.length() > 96 || !doc["seq"].is<uint32_t>() || doc["seq"].as<uint32_t>() <= lease.lastSeq) {
      errorReply(request, 409, "Invalid or stale action ID/sequence"); return;
    }
    if (operation.barrier.active() || program.reserved()) { errorReply(request, 409, "A command or program owns the machine"); return; }
    if (!bothHealthy()) { errorReply(request, 503, "Both Octopus boards must be healthy and synchronized"); return; }
    if (action != "home" && ((boards[0].homed & kHomedMask) != kHomedMask || (boards[1].homed & kHomedMask) != kHomedMask)) {
      errorReply(request, 409, "Home all moving axes first"); return;
    }
    if (animation) {
      const char* error = startBuiltinPreset(action == "next-hour" ? timiac::Preset::Hour : timiac::Preset::Struggle, targetMinute, id, actionHash);
      if (error) { errorReply(request, 409, error); return; }
#if TIMIAC_RELEASE
      releaseClockEnabled = false; // Keep the selected pose until Start clock.
      releaseClockMinute = -1;
#endif
      lease.lastSeq = doc["seq"].as<uint32_t>();
      statusReply(request); return;
    }
    // Discard any caller-supplied G-code and arrays. User actions are defined here.
    doc.remove("gcode");
    timiac::Step step;
    timiac::userAction(nextTime ? "show-time" : action.c_str(), targetMinute / 60, targetMinute % 60, kTravelMm, kMaxFeed, step);
    for (unsigned b = 0; b < 2; ++b) {
      JsonArray lines = doc.createNestedArray(b ? "b" : "a");
      for (unsigned i = 0; i < step.count[b]; ++i) lines.add(String(step.lines[b][i]));
      if (lines.size() != step.count[b]) { errorReply(request, 400, "Action request is too large"); return; }
    }
    doc["actionHash"] = actionHash;
    doc["armMinute"] = action == "show-time" || nextTime ? int(targetMinute) : -1;
    path = "/api/command";
  } else {
    doc.remove("actionHash");
    doc.remove("armMinute");
#if TIMIAC_RELEASE
    if (path == "/api/command" || path.startsWith("/api/program/")) {
      errorReply(request, 404, "Development commands are unavailable in user mode"); return;
    }
#endif
  }
  if (path == "/api/claim") {
    String client = doc["client"] | "";
    if (client.isEmpty() || client.length() > 40) { errorReply(request, 400, "Invalid client ID"); return; }
    if (lease.live() && lease.owner != client) { errorReply(request, 423, "Control belongs to " + lease.label); return; }
    if (!lease.live() || lease.owner != client) {
      ++lease.fence; lease.lastSeq = 0; manualParser = timiac::GcodeState();
      logMessage("Control acquired by " + String(doc["label"] | "Phone").substring(0, 24));
    }
    lease.owner = client; lease.label = String(doc["label"] | "Phone").substring(0, 24); lease.renewed = millis();
  } else if (path == "/api/lease") {
    // authorized() renewed the owner's lease. Observers cannot extend it.
  } else if (path == "/api/release") {
    logMessage("Control released by " + lease.label); lease.owner = "";
  } else if (path == "/api/command") {
    String id = doc["id"] | "";
    timiac::GcodeState nextParser = manualParser;
    uint32_t rawHash = 0;
    if (doc["gcode"].is<const char*>()) {
      rawHash = hashText("raw:" + doc["gcode"].as<String>());
      // Look up raw retries before relative coordinates are translated again.
      for (auto& r : receipts) if (!id.isEmpty() && r.id == id) {
        if (r.hash != rawHash) { errorReply(request, 409, "Request ID reused with different commands"); return; }
        statusReply(request); return;
      }
      for (unsigned b = 0; b < 2; ++b) for (unsigned a = 0; a < 6; ++a) {
        nextParser.position[b * 6 + a] = boards[b].positions[a];
        nextParser.known[b * 6 + a] = boardOnline(b) && boards[b].known[a] && (boards[b].homed & (1U << a));
      }
      const char* error = translate(doc["gcode"], nextParser, doc.as<JsonObject>());
      if (error) { errorReply(request, 400, error); return; }
    }
    if (id.isEmpty() || id.length() > 96 || !validStep(doc)) { errorReply(request, 400, "Invalid command arrays or ID"); return; }
    uint32_t hash = doc["actionHash"].is<uint32_t>() ? doc["actionHash"].as<uint32_t>() : doc["gcode"].is<const char*>() ? rawHash : commandHash(doc["a"], doc["b"]);
    for (auto& r : receipts) if (r.id == id) {
      if (r.hash != hash) { errorReply(request, 409, "Request ID reused with different commands"); return; }
      statusReply(request); return;
    }
    if (!doc["seq"].is<uint32_t>() || doc["seq"].as<uint32_t>() <= lease.lastSeq) {
      errorReply(request, 409, "Stale command sequence; refused to replay"); return;
    }
    if (operation.barrier.active() || program.reserved()) { errorReply(request, 409, "A command or program owns the machine"); return; }
    // Derive the participants from validated commands, never a caller's mask.
    // Clock actions and program playback retain the two-board barrier.
    const uint8_t mask = doc["actionHash"].is<uint32_t>() ? 3 : timiac::commandBoards(doc["a"].size(), doc["b"].size());
    if (!boardsHealthy(mask)) {
      errorReply(request, 503, mask == 1 ? "Octopus 1 must be online, pass driver checks, and finish Stop recovery" :
        mask == 2 ? "Octopus 2 must be online, pass driver checks, and finish Stop recovery" :
        "Both Octopus boards must be healthy and synchronized"); return;
    }
    if (!homingReady(doc)) { errorReply(request, 409, "Home all moving axes first"); return; }
    lease.lastSeq = doc["seq"].as<uint32_t>(); manualParser = nextParser;
    startOperation(doc["a"].as<JsonArray>(), doc["b"].as<JsonArray>(), id, hash, mask);
    armTimeMinutes = !systemLatched ? (doc["armMinute"] | -1) : -1;
    trackManualClockAction(doc["action"] | "");
  } else if (path == "/api/program/begin") {
    unsigned total = doc["total"] | 0;
    if (!total || total > 4096) { errorReply(request, 400, "Program must contain 1–4096 steps"); return; }
    if (operation.barrier.active() || (program.reserved() && program.state != "uploading")) { errorReply(request, 409, "Program/command already active"); return; }
    recordProgramReceipt(); program.file.close(); program = Program();
    program.id = bootId + "-program-" + String(++sequence); program.total = total;
    program.name = String(doc["name"] | "Program").substring(0, 40); program.uploadOwner = lease.owner;
    program.file = LittleFS.open("/program.jsonl", "w");
    if (!program.file) { errorReply(request, 507, "Cannot create program file"); return; }
    program.state = "uploading"; program.activity = millis();
  } else if (path.startsWith("/api/program/")) {
    if (doc["program"] != program.id) { errorReply(request, 409, "Program changed; refresh status"); return; }
    if (path == "/api/program/append") {
      if (program.state != "uploading" || program.uploadOwner != lease.owner || !doc["gcode"].is<const char*>()) {
        errorReply(request, 409, "No matching upload or invalid step"); return;
      }
      unsigned index = doc["index"] | 65535;
      uint32_t hash = hashText(doc["gcode"].as<String>());
      if (program.uploaded && index == program.uploaded - 1 && hash == program.lastHash) { statusReply(request); return; }
      if (index != program.uploaded || index >= program.total) { errorReply(request, 409, "Upload step out of sequence"); return; }
      auto nextParser = program.parser;
      JsonObject step = doc.createNestedObject("step");
      const char* error = translate(doc["gcode"], nextParser, step);
      if (error || !validStep(step)) { errorReply(request, 400, error ? error : "Empty program line"); return; }
      String line; serializeJson(step, line); line += '\n';
      if (program.bytes + line.length() > 524288 || program.file.print(line) != line.length()) {
        program.state = "faulted"; program.file.close(); errorReply(request, 507, "Program storage limit/write failure"); return;
      }
      program.parser = nextParser; program.translation = line; program.bytes += line.length(); program.lastHash = hash; ++program.uploaded; program.activity = millis();
      if (program.uploaded == program.total) {
        program.file.close(); program.state = "ready"; logMessage("Program uploaded: " + program.name);
      }
    } else if (path == "/api/program/start") {
      if (program.state == "running") { statusReply(request); return; }
      if (program.state != "ready" || operation.barrier.active() || !bothHealthy()) { errorReply(request, 409, "Program or both boards are not ready"); return; }
      program.file = LittleFS.open("/program.jsonl", "r");
      if (!program.file) { errorReply(request, 507, "Program file unavailable"); return; }
      program.completed = 0; program.hasStep = false; program.state = "running"; logMessage("Program running on hotspot ESP: " + program.name);
      armTimeMinutes = -1;
    } else if (path == "/api/program/pause") {
      if (program.state != "running" && program.state != "paused") { errorReply(request, 409, "No running program"); return; }
      program.state = "paused"; logMessage("Program pauses at the current step boundary");
    } else if (path == "/api/program/resume") {
      if (program.state != "paused" || !bothHealthy()) { errorReply(request, 409, "Cannot resume until both boards are healthy"); return; }
      program.state = "running"; logMessage("Program resumed");
    } else { errorReply(request, 404, "Unknown program endpoint"); return; }
  } else { errorReply(request, 404, "Unknown endpoint"); return; }
  statusReply(request);
}
void configureHttp() {
  configureLocalFirmwareHttp();
  http.on("/api/status", HTTP_GET, [](AsyncWebServerRequest* request) { statusReply(request); });
  http.on("/api/diagnostics", HTTP_GET, [](AsyncWebServerRequest* request) {
    RuntimeLock lock;
    // Never compete with a TLS handshake/flash operation for the last heap
    // block. Reply promptly instead of attempting a large failed allocation.
    if (firmwareUpdating || platformFreeNetworkHeap() < 16000) {
      errorReply(request, 503, "Controller busy; retry the diagnostic download shortly"); return;
    }
    if (request->hasParam("brief")) {
      DynamicJsonDocument summary(2048);
      summary["software"] = kSoftwareRevision; summary["fault"] = systemFault;
      summary["lastStop"] = lastStopReason; summary["freeHeap"] = platformFreeNetworkHeap();
      summary["largestFreeBlock"] = platformLargestNetworkBlock();
      writeCloudStatus(summary.createNestedObject("cloud"));
      for (unsigned b = 0; b < 2; ++b) {
        auto board = summary.createNestedObject(b ? "b" : "a");
        board["online"] = boardOnline(b); board["fault"] = boards[b].fault;
        board["diagnostic"] = boards[b].diagnostic; board["driverOk"] = boards[b].drivers.good();
      }
      String text; serializeJson(summary, text);
      auto* response = request->beginResponse(200, "application/json", text);
      response->addHeader("Cache-Control", "no-store"); response->addHeader("Connection", "close"); request->send(response); return;
    }
    if (cloudInFlight || platformFreeNetworkHeap() < 48000 || platformLargestNetworkBlock() < 20000) {
      errorReply(request, 503, "Network busy or low memory; retry or use /api/diagnostics?brief=1"); return;
    }
    DynamicJsonDocument document(16384);
    document["boot"] = bootId; document["software"] = kSoftwareRevision;
    writeReliabilityStatus(document.createNestedObject("reliability"));
    writeCloudStatus(document.createNestedObject("cloud"));
    writeUplinkStatus(document.createNestedObject("wifi"));
    writeDiagnosticBatch(document.createNestedArray("persistentEvents"), true);
    JsonArray events = document.createNestedArray("recentEvents");
    const uint32_t first = logSequence > 32 ? logSequence - 32 : 0;
    for (uint32_t i = first; i < logSequence; ++i) {
      const auto& entry = logs[i % 32]; JsonObject event = events.createNestedObject();
      event["id"] = entry.id; event["at"] = entry.at; event["text"] = entry.text;
    }
    if (document.overflowed()) { errorReply(request, 503, "Diagnostic snapshot unavailable; use /api/diagnostics?brief=1"); return; }
    String text; serializeJson(document, text);
    auto* response = request->beginResponse(200, "application/json", text);
    response->addHeader("Cache-Control", "no-store"); response->addHeader("Connection", "close");
    response->addHeader("Content-Disposition", "attachment; filename=\"timiac-diagnostics.json\""); request->send(response);
  });
#if !TIMIAC_RELEASE
  http.on("/api/rtc", HTTP_GET, [](AsyncWebServerRequest* request) {
    RuntimeLock lock;
    StaticJsonDocument<1024> doc; writeRtcStatus(doc.to<JsonObject>(), true); writeNtpStatus(doc.to<JsonObject>());
    String text; serializeJson(doc, text);
    auto* response = request->beginResponse(200, "application/json", text);
    response->addHeader("Cache-Control", "no-store"); request->send(response);
  });
#endif
  for (const char* path : {"/api/claim", "/api/lease", "/api/release", "/api/command", "/api/action", "/api/time", "/api/wifi", "/api/speed", "/api/stop",
       "/api/program/begin", "/api/program/append", "/api/program/start", "/api/program/pause", "/api/program/resume"}) {
    auto* handler = new AsyncCallbackJsonWebHandler(path, [](AsyncWebServerRequest* request, JsonVariant& json) { handleApi(request, json); }, 4096);
    handler->setMethod(HTTP_POST); handler->setMaxContentLength(2300); http.addHandler(handler);
  }
  http.serveStatic("/", LittleFS, "/").setDefaultFile("index.html").setCacheControl("no-store");
  http.serveStatic("/app.js", LittleFS, "/app.js").setCacheControl("no-store");
  http.serveStatic("/control.js", LittleFS, "/control.js").setCacheControl("no-store");
  http.serveStatic("/hardware.js", LittleFS, "/hardware.js").setCacheControl("no-store");
  http.serveStatic("/styles.css", LittleFS, "/styles.css").setCacheControl("no-store");
  http.onNotFound([](AsyncWebServerRequest* request) {
    if (request->url().startsWith("/api/")) errorReply(request, 404, "Unknown endpoint");
    else if (!LittleFS.exists("/index.html")) errorReply(request, 503, "Upload master LittleFS with uploadfs");
    else request->redirect("http://192.168.4.1/");
  });
  http.begin();
}
