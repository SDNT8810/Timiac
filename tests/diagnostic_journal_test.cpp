#include "../shared/diagnostic_journal.h"
#include <cassert>
#include <iostream>
#include <string>
struct Store {
  timiac::DiagnosticEntry entries[timiac::DiagnosticJournal::capacity] = {};
  bool cutPower = false;
  bool read(unsigned slot, timiac::DiagnosticEntry& entry) { entry = entries[slot]; return true; }
  bool write(unsigned slot, const timiac::DiagnosticEntry& entry) {
    entries[slot] = entry;
    if (cutPower) { entries[slot].checksum ^= 1; return false; }
    return true;
  }
};
int main() {
  Store store; timiac::DiagnosticJournal journal; journal.load(store);
  for (unsigned n = 0; n < 100; ++n) {
    const auto text = std::string("OTA master downloading ") + std::to_string(n * 1024);
    assert(journal.append(store, "boot-before-power-cut", n * 10, 1792248000 + n, text.c_str()));
  }
  timiac::DiagnosticJournal rebooted; rebooted.load(store);
  assert(rebooted.sequence == 100);
  for (const auto& record : rebooted.entries) assert(record.sequence >= 77 && record.sequence <= 100);
  assert(rebooted.append(store, "new-boot", 3, 1792250000, "verifying SHA-256; 1150784/1150784"));
  store.cutPower = true;
  assert(!rebooted.append(store, "new-boot", 4, 1792250001, "incomplete flash write"));
  timiac::DiagnosticJournal afterCut; afterCut.load(store);
  assert(afterCut.sequence == 101);
  const auto& latest = afterCut.entries[100 % timiac::DiagnosticJournal::capacity];
  assert(std::string(latest.text) == "verifying SHA-256; 1150784/1150784");
  assert(std::string(latest.boot) == "new-boot" && latest.utc == 1792250000);
  std::cout << "Persistent diagnostic journal: reboot recovery, bounded rotation, corrupt/torn writes and final verification checkpoint passed\n";
}
