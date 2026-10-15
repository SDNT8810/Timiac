#include "../shared/firmware_transfer.h"
#include <cassert>
#include <map>
#include <vector>
#include <string>
#include <sstream>
#include <algorithm>
#include <iostream>

std::map<std::string, std::vector<uint8_t>> files;
bool writeFail = false, readCorrupt = false, renameFail = false, cardMissing = false, rebooted = false;
std::ostringstream serial;
constexpr int O_CREAT = 1, O_TRUNC = 2, O_RDWR = 4;
struct SdFile {
  std::string name; unsigned position = 0;
  bool exists(const char* path) { return files.count(path); }
  bool open(SdFile*, const char* path, int flags) {
    name = path; position = 0;
    if (flags & O_TRUNC) files[name].clear();
    return true;
  }
  void close() { name = ""; }
  int16_t write(const void* input, uint16_t count) {
    if (writeFail || name.empty()) return -1;
    auto& data = files[name]; data.resize(position + count);
    memcpy(data.data() + position, input, count); position += count; return count;
  }
  int16_t read(void* output, uint16_t count) {
    auto& data = files[name]; count = std::min<unsigned>(count, data.size() - position);
    memcpy(output, data.data() + position, count); position += count;
    if (readCorrupt && count) static_cast<uint8_t*>(output)[0] ^= 1;
    return count;
  }
  bool sync() { return !name.empty(); }
  uint32_t fileSize() { return files[name].size(); }
  bool seekSet(unsigned offset) { position = offset; return offset <= files[name].size(); }
  bool rename(SdFile*, const char* path) {
    if (renameFail) return false;
    files[path] = files[name]; files.erase(name); name = path; return true;
  }
};
struct FakeCard { bool isPrinting() { return false; } bool isFileOpen() { return false; } void mount() {} bool isMounted() { return !cardMissing; } SdFile getroot() { return SdFile(); } } card;
struct FakePlanner { void synchronize() {} } planner;
struct FakeHal { void watchdog_refresh() {} } hal;
struct FakeParser { const char* command_ptr; } parser;
struct GcodeSuite { static void M997(); };
void flashFirmware(int) { rebooted = true; }
void safe_delay(unsigned) {}
void SERIAL_FLUSHTX() {}
#define SERIAL_ECHOPGM(x) (serial << (x))
#define SERIAL_ECHOLN(x) (serial << (x) << '\n')
#define SERIAL_ECHOLNPGM(x) (serial << (x) << '\n')
#define TIMIAC_HOST_OTA_TEST
#define TIMIAC_FIRMWARE_REVISION "2026-10-15.5"
#include "../Marlin/timiac_sd_ota.cpp"

std::string command(const std::string& line) {
  serial.str(""); serial.clear(); parser.command_ptr = line.c_str(); GcodeSuite::M997(); return serial.str();
}
uint32_t crc(const std::vector<uint8_t>& bytes) { return timiac::firmwareCrc(0xFFFFFFFFUL, bytes.data(), bytes.size()) ^ 0xFFFFFFFFUL; }
std::string block(const std::vector<uint8_t>& image, unsigned offset) {
  std::vector<uint8_t> bytes(image.begin() + offset, image.begin() + std::min<unsigned>(offset + 64, image.size()));
  std::string hex; const char digits[] = "0123456789abcdef";
  for (uint8_t byte : bytes) { hex += digits[byte >> 4]; hex += digits[byte & 15]; }
  return "M997 P1 " + std::to_string(offset) + " " + std::to_string(crc(bytes)) + " " + hex;
}
void reset() { command("M997 P4"); files.clear(); rebooted = writeFail = readCorrupt = renameFail = cardMissing = false; }
void send(const std::vector<uint8_t>& image, unsigned limit = 0) {
  assert(command("M997 P0 " + std::to_string(image.size()) + " " + std::to_string(crc(image))).find("OFFSET 0") != std::string::npos);
  if (!limit) limit = image.size();
  for (unsigned offset = 0; offset < limit; offset += 64) {
    const auto line = block(image, offset);
    assert(command(line).find("TIMIAC_FW_OFFSET") != std::string::npos);
    assert(command(line).find("TIMIAC_FW_OFFSET") != std::string::npos); // Lost ACK/retry must not write twice.
  }
}
int main() {
  std::vector<uint8_t> image(2048); for (unsigned i = 0; i < image.size(); ++i) image[i] = i * 13;
  const uint8_t header[] = {0,0,2,0x20,1,0x81,0,8}; memcpy(image.data(), header, 8);
  assert(timiac::stm32FirmwareHeader(image.data(), image.size()));
  for (const char* invalid : {"M997", "M997 P3 extra", "M997 P6", "M997 P0 4294967296 1", "M997 P0 999 1", "M997 P1 0 0 ff", "M997 P1 0 0 0"}) {
    timiac::FirmwareCommand decoded; assert(!timiac::parseFirmwareCommand(invalid, decoded));
  }
  reset(); send(image);
  assert(!files.count("firmware.bin") && !rebooted);
  assert(command("M997 P3").find("verify before") != std::string::npos && !rebooted);
  assert(command("M997 P2").find("VERIFIED") != std::string::npos);
  assert(command("M997 P3").find("REBOOT") != std::string::npos && rebooted);
  assert(files["firmware.bin"] == image && !files.count("timiac.upd"));
  reset(); send(image, 128);
  assert(command("M997 P2").find("incomplete") != std::string::npos);
  command("M997 P3"); assert(!rebooted && !files.count("firmware.bin"));
  command("M997 P4"); send(image); // Recover a disconnected upload without resetting the board.
  readCorrupt = true;
  assert(command("M997 P2").find("readback checksum") != std::string::npos);
  command("M997 P3"); assert(!rebooted && !files.count("firmware.bin"));
  reset(); send(image); command("M997 P2"); readCorrupt = true;
  assert(command("M997 P2").find("readback checksum") != std::string::npos);
  command("M997 P3"); assert(!rebooted && !files.count("firmware.bin")); // A failed recheck invalidates a prior verification.
  reset(); send(image); command("M997 P2"); renameFail = true;
  assert(command("M997 P3").find("cannot activate") != std::string::npos && !rebooted);
  reset(); files["firmware.bin"] = image;
  assert(command("M997 P0 2048 1").find("already pending") != std::string::npos);
  reset(); cardMissing = true;
  assert(command("M997 P0 2048 1").find("microSD") != std::string::npos);
  reset(); command("M997 P0 2048 " + std::to_string(crc(image))); writeFail = true;
  assert(command(block(image, 0)).find("write failed") != std::string::npos && !files.count("firmware.bin"));
  reset(); image[0] = 0xE9; command("M997 P0 2048 " + std::to_string(crc(image)));
  assert(command(block(image, 0)).find("wrong STM32") != std::string::npos && !rebooted);
  std::cout << "Production SD updater: retries, truncation, wrong images, SD faults, CRC readback and verified activation passed\n";
}
