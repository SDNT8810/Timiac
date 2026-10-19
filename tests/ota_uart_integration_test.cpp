// Exercise BOTH production ends of the SD protocol through a 63-byte UART RX
// queue, while the Octopus main loop is blocked for each complete command.
#define main bridgeRegressionMain
#include "bridge_link_test.cpp"
#undef main
#define main sdReceiverRegressionMain
#include "firmware_update_test.cpp"
#undef main

struct {
  String kind = "octopus", revision = kSoftwareRevision, state = "waiting for UART", phase, error;
  uint32_t size = 32773, done = 0, crc32 = 0; // Includes a final short block.
} firmwareUpdate;
void otaFailed(const String& reason) { firmwareUpdate.state = "failed"; firmwareUpdate.error = reason; firmwareUpdating = firmwareUpdateCancel = false; }
String otaHex(const uint8_t* bytes, size_t size) {
  const char digits[] = "0123456789abcdef"; std::string text;
  while (size--) { text += digits[*bytes >> 4]; text += digits[*bytes++ & 15]; }
  return text;
}
struct { void remove(const char*) {} } LittleFS;
String otaSerialLine;
#include "../ESP_Timiac/src/ota_octopus.h"

int main() {
  std::vector<uint8_t> image(firmwareUpdate.size);
  for (unsigned i = 0; i < image.size(); ++i) image[i] = i * 13;
  const uint8_t header[] = {0,0,2,0x20,1,0x81,0,8}; memcpy(image.data(), header, 8);
  // Reproduce the old oversized frame losing its newline when the CPU pauses.
  const std::string oldFrame = block(image, 512) + '\n';
  assert(oldFrame.size() > 63 && oldFrame.substr(0, 63).find('\n') == std::string::npos);

  reset(); setupBridge(); firmwareUpdating = true; firmwareUpdateCancel = false;
  firmwareUpdate.crc32 = crc(image); uart.session.waiting = false;
  octopusImage.bytes = image; octopusSent = hostNow;
  octopusTick(); size_t consumed = 0; unsigned blocks = 0;
  // An earlier truncated attempt may have left an unfinished command. The
  // leading newline + abort handshake must fence its error before restarting.
  const std::string abandoned = "M997 P1 512 123 broken";
  std::string partialLine = abandoned;
  for (unsigned turn = 0; octopusPhase != 5 && turn < 10000; ++turn) {
    const std::string burst = Serial.sent.substr(consumed); consumed = Serial.sent.size();
    assert(!burst.empty());
    // Simulate all bytes arriving before Marlin gets CPU time to read them.
    assert(burst.size() <= 63);
    std::string replies;
    for (char c : burst) {
      if (c != '\n') { partialLine += c; continue; }
      if (partialLine.empty()) continue;
      timiac::FirmwareCommand decoded;
      const bool valid = timiac::parseFirmwareCommand(partialLine.c_str(), decoded);
      assert(valid || partialLine == abandoned);
      if (valid && decoded.type == 1) ++blocks;
      replies += command(partialLine) + "ok\n"; partialLine.clear();
    }
    assert(!replies.empty()); Serial.feed(replies); hostNow += 20; octopusTick();
    assert(firmwareUpdate.state != "failed");
  }
  assert(octopusPhase == 5 && rebooted && files["firmware.bin"] == image);
  assert(blocks == (image.size() + 15) / 16 && firmwareUpdate.done == image.size());
  assert(!files.count("timiac.upd"));
  std::cout << "Production bridge + SD receiver: old oversized UART frame loses its delimiter; complete 32 KB verified update fits the installed 63-byte RX queue through CPU stalls\n";
}
