#pragma once
struct LogEntry { uint32_t id = 0, at = 0; String text; };
LogEntry logs[32];
uint32_t logSequence = 0, sequence = 0;
String bootId, coordinatorBoot, serialLine;
String bridgeResetReason;
uint32_t lastPeerRejection = 0;
bool serialDiscard = false, stopRequested = false, systemLatched = false, stopPending = false;
bool networkChanging = false;
String systemFault, stopToken, stopReason, acknowledgedStop;
uint8_t stopRequired = 0, stopRecoveryRequired = 0;
uint32_t lastStateTx = 0, reconnectAt = 0, reconnectDelay = 500, lastWifiRetry = 0;

struct Peer {
  WiFiClient socket;
  String line, boot, closeReason;
  timiac::PeerOutbox outbox;
  int board = -1;
  bool paired = false, discard = false;
  uint32_t rx = 0, tx = 0;
} peers[TIMIAC_MASTER ? 2 : 1];
struct RemoteBoard {
  timiac::EndstopReport endstops;
  timiac::DriverReport drivers;
  String firmware;
  String diagnostic, software;
  uint32_t uptime = 0;
  bool driverReportAvailable = false;
  bool trajectorySupported = false;
  uint32_t driverReportAt = 0;
  bool online = false, busy = false, latched = false, resetRequired = false;
  uint8_t homed = 0;
  uint32_t reset = 0, stateAt = 0;
  String fault, stopAck;
  float positions[6] = {};
  bool known[6] = {};
} boards[2];
struct Operation {
  String id, error;
  uint32_t hash = 0;
  bool homing = false;
  timiac::Barrier barrier;
} operation;
struct Receipt { String id, state, error; uint32_t hash = 0; };
Receipt receipts[16];
unsigned receiptNext = 0;
struct Lease {
  String owner, label;
  uint32_t fence = 0, renewed = 0, lastSeq = 0;
  bool live() const { return !owner.isEmpty() && !timiac::expired(millis(), renewed, kLeaseMs); }
} lease;
timiac::GcodeState manualParser;
int armTimeMinutes = -1;
int releaseClockMinute = -1; // Last automatic target; survives a proximity animation.
bool releaseClockEnabled = false;
bool releaseStartupPending = TIMIAC_MASTER && TIMIAC_RELEASE;
struct Program {
  String id, name, state = "empty", error, uploadOwner, translation;
  unsigned total = 0, uploaded = 0, completed = 0;
  uint32_t bytes = 0, lastHash = 0, activity = 0;
  File file;
  timiac::GcodeState parser;
  bool hasStep = false;
  bool builtin = false;
  timiac::Preset preset = timiac::Preset::Struggle;
  unsigned targetMinute = 0;
  uint32_t actionHash = 0;
  bool reserved() const { return state == "uploading" || state == "running" || state == "paused"; }
} program;
void recordProgramReceipt() {
  if (!program.builtin) return;
  Receipt* target = nullptr;
  for (auto& r : receipts) if (r.id == program.id) { target = &r; break; }
  if (!target) { target = &receipts[receiptNext]; receiptNext = (receiptNext + 1) % 16; }
  target->id = program.id; target->hash = program.actionHash;
  target->state = program.state == "faulted" ? "failed" : program.state;
  target->error = program.error;
}
void logMessage(const String& message);
bool sendPeer(Peer& peer, JsonDocument& doc);
void sendState();
void stopAll(const String& reason);
bool bothOnline();
bool bothHealthy();
bool boardsHealthy(uint8_t mask);
void finishLocal();
void startOperation(JsonArray a, JsonArray b, const String& id, uint32_t hash, uint8_t mask = 3, bool trajectory = false);
void recordReceipt() {
  for (auto& r : receipts) if (r.id == operation.id) {
    r.state = operation.barrier.name(); r.error = operation.error; return;
  }
  Receipt& receipt = receipts[receiptNext];
  receipt.id = operation.id; receipt.state = operation.barrier.name();
  receipt.error = operation.error; receipt.hash = operation.hash;
  receiptNext = (receiptNext + 1) % 16;
}
uint32_t hashText(const String& text) {
  uint32_t hash = 2166136261UL;
  for (unsigned i = 0; i < text.length(); ++i) hash = (hash ^ uint8_t(text[i])) * 16777619UL;
  return hash;
}
uint32_t commandHash(JsonVariant a, JsonVariant b) {
  String text; serializeJson(a, text); text += '|'; serializeJson(b, text);
  return hashText(text);
}
bool validLines(JsonVariant value, bool trajectory = false) {
  if (!value.is<JsonArray>() || value.size() > (trajectory ? timiac::maxTrajectoryCommands : kMaxCommands)) return false;
  for (JsonVariant entry : value.as<JsonArray>())
    if (!entry.is<const char*>() || !timiac::validNative(entry.as<const char*>(), kAxisCount, kTravelMm, kMaxFeed) ||
        (trajectory && !timiac::trajectoryCommand(entry.as<const char*>()))) return false;
  return true;
}
bool validStep(JsonVariant step) {
  return validLines(step["a"]) && validLines(step["b"]) && (step["a"].size() || step["b"].size());
}
bool hasMove(JsonArray lines) {
  for (JsonVariant value : lines) if (String(value.as<const char*>()).startsWith("G1 ")) return true;
  return false;
}
bool hasHoming(JsonArray lines) {
  for (JsonVariant value : lines) if (timiac::homingCommand(value.as<const char*>())) return true;
  return false;
}
bool homingReady(JsonVariant step) {
  for (unsigned b = 0; b < 2; ++b)
    for (JsonVariant line : step[b == 0 ? "a" : "b"].as<JsonArray>()) {
      const char* command = line.as<const char*>();
      if (!strncmp(command, "G1 ", 3))
        for (unsigned a = 0; a < 6; ++a)
          if (strchr(command, kAxes[a]) && !(boards[b].homed & (1U << a))) return false;
    }
  return true;
}
const char* translate(const char* line, timiac::GcodeState& state, JsonObject doc) {
  timiac::Step step;
  const char* error = timiac::compileLine(line, state, step, kAxisCount, kTravelMm, kMaxFeed);
  if (error) return error;
  for (unsigned b = 0; b < 2; ++b) {
    JsonArray lines = doc.createNestedArray(b == 0 ? "a" : "b");
    for (unsigned i = 0; i < step.count[b]; ++i) lines.add(step.lines[b][i]);
  }
  return nullptr;
}
