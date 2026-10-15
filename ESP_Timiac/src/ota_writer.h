#pragma once
#ifndef TIMIAC_HOST_OTA_TEST
#if defined(ARDUINO_ARCH_ESP32)
#include <Update.h>
#else
#include <Updater.h>
#endif
#include "ota_crypto.h"
#endif

struct OtaWriter {
  String kind, expectedHash, error;
  uint32_t size = 0, received = 0;
  bool active = false, flash = false;
  uint8_t header[8] = {}; unsigned headerCount = 0;
  File file;
  OtaSha256 sha;
  bool begin(const String& type, uint32_t bytes, const String& hash) {
    kind = type; size = bytes; expectedHash = hash; received = 0; headerCount = 0; error = ""; flash = type != "octopus";
    if (!bytes || !otaHashValid(hash)) { error = "Invalid size or SHA-256"; return false; }
    if (type == "octopus") {
      if (bytes > timiac::octopusFirmwareLimit || !LittleFS.begin()) { error = "Octopus staging storage unavailable"; return false; }
      file = LittleFS.open("/octopus.update", "w");
      if (!file) { error = "Cannot create Octopus staging file"; return false; }
    } else {
      if (type != "app" && type != "filesystem") { error = "Unsupported image type"; return false; }
#if defined(ARDUINO_ARCH_ESP8266)
      if (type != "app") { error = "Bridge accepts app images only"; return false; }
      Update.runAsync(true);
#else
      if (type == "filesystem") LittleFS.end();
#endif
      if (!Update.begin(bytes, type == "app" ? U_FLASH : 100)) { error = "Image does not fit its update partition"; return false; }
    }
    sha.begin(); active = true; return true;
  }
  bool write(uint8_t* bytes, size_t count) {
    if (!active || count > size - received) { error = "Unexpected update length"; return false; }
    for (unsigned i = 0; i < count && headerCount < sizeof(header); ++i) header[headerCount++] = bytes[i];
    if ((!received && kind == "app" && bytes[0] != 0xE9) ||
        (kind == "octopus" && received < 8 && headerCount == 8 && !timiac::stm32FirmwareHeader(header, 8))) { error = "Wrong firmware image"; return false; }
    const size_t written = flash ? Update.write(bytes, count) : file.write(bytes, count);
    if (written != count) { error = "Flash or staging write failed"; return false; }
    sha.write(bytes, count); received += count; return true;
  }
  void abort() {
    if (active && flash) {
#if defined(ARDUINO_ARCH_ESP32)
      Update.abort();
#else
      // ESP8266 has no abort API. A deliberately wrong MD5 prevents end()
      // from ever activating even a full-size but SHA-rejected image.
      Update.setMD5("00000000000000000000000000000000"); Update.end();
#endif
    }
    file.close(); active = false;
#if defined(ARDUINO_ARCH_ESP32)
    if (kind == "filesystem") LittleFS.begin();
#endif
  }
  bool finish() {
    if (!active || received != size || !otaEqual(sha.finish(), expectedHash)) { error = "Incomplete image or SHA-256 mismatch"; abort(); return false; }
    if (flash && !Update.end()) { error = "Firmware activation failed"; abort(); return false; }
    file.close(); active = false;
#if defined(ARDUINO_ARCH_ESP32)
    if (kind == "filesystem" && !LittleFS.begin()) { error = "Updated filesystem did not mount"; return false; }
#endif
    return true;
  }
};
