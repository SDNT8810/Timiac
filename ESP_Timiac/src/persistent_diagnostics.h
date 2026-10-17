#pragma once
String lastUpdateCheckpoint;
uint32_t diagnosticWriteFailures = 0;
#if TIMIAC_MASTER && defined(ARDUINO_ARCH_ESP32)
timiac::DiagnosticJournal diagnosticJournal;
struct NvsDiagnosticStore {
  Preferences& preferences;
  String key(unsigned slot) { return "e" + String(slot); }
  bool read(unsigned slot, timiac::DiagnosticEntry& entry) { return preferences.getBytes(key(slot).c_str(), &entry, sizeof(entry)) == sizeof(entry); }
  bool write(unsigned slot, const timiac::DiagnosticEntry& entry) { return preferences.putBytes(key(slot).c_str(), &entry, sizeof(entry)) == sizeof(entry); }
};
#endif
void persistDiagnostic(const String& text) {
#if TIMIAC_MASTER && defined(ARDUINO_ARCH_ESP32)
  Preferences history;
  if (!history.begin("timiac-diag", false)) { ++diagnosticWriteFailures; return; }
  NvsDiagnosticStore store{history};
  const time_t utc = time(nullptr);
  if (!diagnosticJournal.append(store, bootId.c_str(), millis(), utc > 1700000000 ? uint32_t(utc) : 0, text.c_str())) ++diagnosticWriteFailures;
  history.end();
#endif
}
void configurePersistentDiagnostics() {
#if TIMIAC_MASTER && defined(ARDUINO_ARCH_ESP32)
  Preferences history;
  if (history.begin("timiac-diag", true)) {
    NvsDiagnosticStore store{history}; diagnosticJournal.load(store);
    lastUpdateCheckpoint = history.getString("update", ""); history.end();
  }
  persistDiagnostic("Master boot " + String(kSoftwareRevision) + "; reset reason " + String(unsigned(esp_reset_reason())));
#endif
}
void persistUpdateCheckpoint(const String& checkpoint) {
  lastUpdateCheckpoint = checkpoint;
#if TIMIAC_MASTER && defined(ARDUINO_ARCH_ESP32)
  Preferences history;
  if (history.begin("timiac-diag", false)) {
    if (!history.putString("update", checkpoint)) ++diagnosticWriteFailures;
    history.end();
  } else ++diagnosticWriteFailures;
#elif !TIMIAC_MASTER && defined(ARDUINO_ARCH_ESP8266)
  File file = LittleFS.open("/ota-last.tmp", "w");
  if (!file) { ++diagnosticWriteFailures; return; }
  const size_t written = file.print(checkpoint); file.flush(); file.close();
  if (written != checkpoint.length() || !LittleFS.rename("/ota-last.tmp", "/ota-last.json")) ++diagnosticWriteFailures;
#endif
}
void writeSavedUpdateCheckpoint(JsonObject target) {
  if (lastUpdateCheckpoint.isEmpty()) return;
  DynamicJsonDocument checkpoint(1024);
  if (!deserializeJson(checkpoint, lastUpdateCheckpoint)) target.set(checkpoint.as<JsonObject>());
}
void writeDiagnosticBatch(JsonArray events, bool all = false) {
#if TIMIAC_MASTER && defined(ARDUINO_ARCH_ESP32)
  static unsigned cursor = 0;
  const unsigned count = all ? timiac::DiagnosticJournal::capacity : 3;
  for (unsigned n = 0; n < count; ++n) {
    const auto& record = diagnosticJournal.entries[(all ? n : cursor++) % timiac::DiagnosticJournal::capacity];
    if (!record.sequence) continue;
    JsonObject event = events.createNestedObject(); event["id"] = "p" + String(record.sequence);
    event["boot"] = record.boot; event["sequence"] = record.sequence;
    event["at"] = record.uptime; event["utc"] = record.utc; event["text"] = record.text; event["persistent"] = true;
  }
#endif
}
