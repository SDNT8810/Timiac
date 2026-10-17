#pragma once
#include "firmware_transfer.h"
#include <stddef.h>

namespace timiac {
struct DiagnosticEntry {
  uint32_t magic, sequence, uptime, utc;
  char boot[40], text[180];
  uint32_t checksum;
};
// Small independent NVS records: a torn write loses at most one record, and the
// journal fits alongside Wi-Fi/OTA settings in the existing NVS partition.
struct DiagnosticJournal {
  static constexpr unsigned capacity = 24;
  DiagnosticEntry entries[capacity] = {};
  uint32_t sequence = 0;
  static uint32_t checksum(const DiagnosticEntry& entry) {
    return firmwareCrc(0xFFFFFFFFUL, reinterpret_cast<const uint8_t*>(&entry), offsetof(DiagnosticEntry, checksum)) ^ 0xFFFFFFFFUL;
  }
  template<class Store> void load(Store& store) {
    sequence = 0;
    for (unsigned i = 0; i < capacity; ++i) {
      DiagnosticEntry entry = {};
      if (store.read(i, entry) && entry.magic == 0x54444A31 && entry.sequence &&
          entry.boot[sizeof(entry.boot)-1] == 0 && entry.text[sizeof(entry.text)-1] == 0 && entry.checksum == checksum(entry)) {
        entries[i] = entry; if (entry.sequence > sequence) sequence = entry.sequence;
      } else entries[i] = DiagnosticEntry{};
    }
  }
  template<class Store> bool append(Store& store, const char* boot, uint32_t uptime, uint32_t utc, const char* text) {
    DiagnosticEntry entry = {};
    entry.magic = 0x54444A31; entry.sequence = sequence + 1; entry.uptime = uptime; entry.utc = utc;
    strncpy(entry.boot, boot, sizeof(entry.boot)-1); strncpy(entry.text, text, sizeof(entry.text)-1);
    entry.checksum = checksum(entry);
    const unsigned slot = (entry.sequence - 1) % capacity;
    if (!store.write(slot, entry)) return false;
    entries[slot] = entry; sequence = entry.sequence; return true;
  }
};
}
