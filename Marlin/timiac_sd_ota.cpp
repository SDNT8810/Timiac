// Timiac SD firmware receiver. Installed into pinned Marlin by build.py.
// The factory 32 KB BTT bootloader is preserved. Only a fully verified staging
// file is renamed firmware.bin; a failed/interrupted upload cannot activate it.
#ifndef TIMIAC_HOST_OTA_TEST
#include "../gcode.h"
#include "../../module/planner.h"
#include "../../sd/cardreader.h"
#include "../../feature/timiac_update_protocol.h"
#endif

namespace {
SdFile updateFile;
bool updateActive = false, updateVerified = false;
uint32_t updateSize = 0, updateOffset = 0, updateExpectedCrc = 0, updateCrc = 0xFFFFFFFFUL;
uint32_t previousOffset = 0, previousCrc = 0;
unsigned previousCount = 0;
constexpr char stagingName[] = "timiac.upd";
void updateError(const char* message) { SERIAL_ECHOPGM("TIMIAC_FW_ERROR "); SERIAL_ECHOLN(message); }
void updateAck() { SERIAL_ECHOPGM("TIMIAC_FW_OFFSET "); SERIAL_ECHOLN(updateOffset); }
}

void GcodeSuite::M997() {
  timiac::FirmwareCommand command;
  if (!timiac::parseFirmwareCommand(parser.command_ptr, command)) { updateError("invalid command or block checksum"); return; }
  if (command.type == 5) { SERIAL_ECHOLNPGM("TIMIAC_OTA_ROLE:octopus " TIMIAC_FIRMWARE_REVISION); return; }
  if (command.type == 4) {
    updateFile.close(); updateActive = updateVerified = false;
    SERIAL_ECHOLNPGM("TIMIAC_FW_ABORTED"); return;
  }
  if (command.type == 0) {
    if (updateActive) {
      if (command.offset == updateSize && command.crc == updateExpectedCrc && !updateOffset) updateAck();
      else updateError("upload already active");
      return;
    }
    if (card.isPrinting() || card.isFileOpen()) { updateError("SD card busy"); return; }
    planner.synchronize(); card.mount();
    if (!card.isMounted()) { updateError("insert a writable FAT32 microSD card"); return; }
    auto root = card.getroot();
    if (root.exists("firmware.bin")) { updateError("firmware.bin already pending on card"); return; }
    updateFile.close();
    if (!updateFile.open(&root, stagingName, O_CREAT | O_TRUNC | O_RDWR)) { updateError("cannot open staging file"); return; }
    updateActive = true; updateVerified = false;
    updateSize = command.offset; updateExpectedCrc = command.crc; updateOffset = 0;
    updateCrc = 0xFFFFFFFFUL; previousCount = 0;
    updateAck(); return;
  }
  if (!updateActive) { updateError("no upload"); return; }
  if (command.type == 1) {
    if (previousCount && command.offset == previousOffset && command.crc == previousCrc && command.count == previousCount) { updateAck(); return; }
    if (updateVerified || command.offset != updateOffset || command.count > updateSize - updateOffset) { updateError("wrong block offset"); return; }
    if (!updateOffset && !timiac::stm32FirmwareHeader(command.bytes, command.count)) { updateError("wrong STM32 image or bootloader offset"); return; }
    if (updateFile.write(command.bytes, command.count) != int16_t(command.count)) { updateError("SD write failed"); return; }
    updateCrc = timiac::firmwareCrc(updateCrc, command.bytes, command.count);
    previousOffset = command.offset; previousCrc = command.crc; previousCount = command.count;
    updateOffset += command.count; updateAck(); return;
  }
  if (command.type == 2) {
    updateVerified = false;
    if (updateOffset != updateSize || (updateCrc ^ 0xFFFFFFFFUL) != updateExpectedCrc || !updateFile.sync() ||
        updateFile.fileSize() != updateSize || !updateFile.seekSet(0)) { updateError("incomplete image or stream checksum"); return; }
    uint8_t buffer[256]; uint32_t crc = 0xFFFFFFFFUL, remaining = updateSize;
    while (remaining) {
      const uint16_t wanted = remaining < sizeof(buffer) ? remaining : sizeof(buffer);
      if (updateFile.read(buffer, wanted) != wanted) { updateError("SD readback failed"); return; }
      crc = timiac::firmwareCrc(crc, buffer, wanted); remaining -= wanted; hal.watchdog_refresh();
    }
    if ((crc ^ 0xFFFFFFFFUL) != updateExpectedCrc) { updateError("SD readback checksum mismatch"); return; }
    updateVerified = true; SERIAL_ECHOLNPGM("TIMIAC_FW_VERIFIED"); return;
  }
  if (command.type == 3) {
    if (!updateVerified) { updateError("verify before activation"); return; }
    auto root = card.getroot();
    if (!updateFile.rename(&root, "firmware.bin") || !updateFile.sync()) { updateError("cannot activate verified image"); return; }
    updateFile.close(); updateActive = false;
    SERIAL_ECHOLNPGM("TIMIAC_FW_REBOOT"); SERIAL_FLUSHTX(); safe_delay(100);
    flashFirmware(0);
  }
}
