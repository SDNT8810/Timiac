#define main bridgeRegressionMain
#include "bridge_link_test.cpp"
#undef main
#include "../shared/firmware_transfer.h"
struct {
  String kind = "octopus", revision = kSoftwareRevision, state = "waiting for UART", error;
  uint32_t size = 1024, done = 0, crc32 = 123;
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

String lastCommand() {
  const auto start = Serial.sent.rfind("M997 "); assert(start != std::string::npos);
  return Serial.sent.substr(start, Serial.sent.find('\n', start) - start);
}
int main() {
  setupBridge(); firmwareUpdating = true; firmwareUpdateCancel = false;
  uart.session.waiting = false; octopusImage.bytes.assign(1024, 42);
  octopusSent = hostNow; octopusTick(); assert(lastCommand() == "M997 P4");
  Serial.feed("echo:TIMIAC_FW_ABORTED\r\n"); octopusTick(); assert(octopusPhase == 1);
  Serial.feed("TIMIAC_FW_OFFSET 0\n"); octopusTick(); assert(octopusPhase == 2);
  const String first = lastCommand();
  Serial.feed("TIMIAC_FW_OFFSET 0\n"); octopusTick(); assert(lastCommand() == first && firmwareUpdate.done == 0);
  hostNow += 3001; octopusTick(); assert(lastCommand() == first && octopusRetries == 1);
  while (octopusPhase == 2) {
    timiac::FirmwareCommand command;
    assert(timiac::parseFirmwareCommand(lastCommand().c_str(), command));
    assert(command.type == 1 && command.offset == octopusOffset && command.count == 64);
    Serial.feed("TIMIAC_FW_OFFSET " + std::to_string(octopusNextOffset) + "\n"); octopusTick();
  }
  assert(octopusPhase == 3 && firmwareUpdate.done == 1024 && lastCommand() == "M997 P2");
  Serial.feed("TIMIAC_FW_VERIFIED\n"); octopusTick(); assert(octopusPhase == 4 && lastCommand() == "M997 P3");
  const std::string atActivation = Serial.sent;
  hostNow += 5000; firmwareUpdateCancel = true; octopusTick();
  assert(octopusPhase == 4 && firmwareUpdating && Serial.sent == atActivation);
  Serial.feed("start\n"); octopusTick(); assert(octopusPhase == 5);
  uart.session.state = timiac::OctopusSession::State::Ready; uart.session.identity = true; uart.session.lastRx = hostNow;
  uart.sdOtaSupported = true; uart.octopusSoftware = kSoftwareRevision;
  octopusTick(); assert(firmwareUpdate.state == "complete" && !firmwareUpdating);
  firmwareUpdating = true; firmwareUpdateCancel = false; octopusPhase = 2;
  Serial.feed("Error:SD card unavailable\n"); octopusTick();
  assert(firmwareUpdate.state == "failed" && firmwareUpdate.error == "Error:SD card unavailable");
  std::cout << "Production Octopus sender: stale ACKs, exact block CRC/offsets, safe retries and one-shot activation with delayed reboot passed\n";
}
