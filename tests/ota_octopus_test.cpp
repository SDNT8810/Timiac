#define main bridgeRegressionMain
#include "bridge_link_test.cpp"
#undef main
#include "../shared/firmware_transfer.h"
struct {
  String id = "11111111-1111-4111-8111-111111111111", target = "octopus1", sha256 = std::string(64, 'a');
  String kind = "octopus", revision = kSoftwareRevision, state = "waiting for UART", phase, error;
  uint32_t size = 1024, done = 0, crc32 = 123;
} firmwareUpdate;
void otaFailed(const String& reason) { firmwareUpdate.state = "failed"; firmwareUpdate.error = reason; firmwareUpdating = firmwareUpdateCancel = false; }
String otaHex(const uint8_t* bytes, size_t size) {
  const char digits[] = "0123456789abcdef"; std::string text;
  while (size--) { text += digits[*bytes >> 4]; text += digits[*bytes++ & 15]; }
  return text;
}
#include "host/ReceiptStore.h"
ReceiptStore LittleFS;
bool otaHashValid(const String& value) { return value.length() == 64; }
String otaSerialLine;
#include "../ESP_Timiac/src/ota_octopus.h"
#include "../ESP_Timiac/src/ota_octopus_receipt.h"

String lastCommand() {
  const auto start = Serial.sent.rfind("M997 "); assert(start != std::string::npos);
  return Serial.sent.substr(start, Serial.sent.find('\n', start) - start);
}
int main() {
  setupBridge(); firmwareUpdating = true; firmwareUpdateCancel = false;
  localUart().session.waiting = false; octopusImage.bytes.assign(1024, 42);
  octopusSent = hostNow; octopusTick(); assert(lastCommand() == "M997 P4");
  Serial.feed("echo:TIMIAC_FW_ABORTED\r\n"); octopusTick(); assert(octopusPhase == 1);
  Serial.feed("TIMIAC_FW_OFFSET 0\n"); octopusTick(); assert(octopusPhase == 2);
  const String first = lastCommand();
  Serial.feed("TIMIAC_FW_OFFSET 0\n"); octopusTick(); assert(lastCommand() == first && firmwareUpdate.done == 0);
  hostNow += 3001; octopusTick(); assert(lastCommand() == first && !octopusRetries);
  hostNow += 7000; octopusTick(); assert(lastCommand() == first && octopusRetries == 1);
  while (octopusPhase == 2) {
    timiac::FirmwareCommand command;
    assert(timiac::parseFirmwareCommand(lastCommand().c_str(), command));
    assert(command.type == 1 && command.offset == octopusOffset && command.count == 16);
    Serial.feed("TIMIAC_FW_OFFSET " + std::to_string(octopusNextOffset) + "\n"); octopusTick();
  }
  assert(octopusPhase == 3 && firmwareUpdate.done == 1024 && lastCommand() == "M997 P2");
  hostNow += 10001; octopusTick(); assert(octopusPhase == 3 && !octopusRetries);
  Serial.feed("TIMIAC_FW_VERIFIED\n"); octopusTick(); assert(octopusPhase == 4 && lastCommand() == "M997 P3");
  const std::string atActivation = Serial.sent;
  hostNow += 5000; firmwareUpdateCancel = true; octopusTick();
  assert(octopusPhase == 4 && firmwareUpdating && Serial.sent == atActivation);
  Serial.feed("start\n"); octopusTick(); assert(octopusPhase == 5);
  // Reproduce the real log: SD activation succeeded, then its ESP reset and
  // previously restored the unrelated older ESP-app receipt. The exact SD job
  // must survive this reset without sending another activation command.
  assert(LittleFS.exists("/octopus-receipt.json"));
  const String expectedId = firmwareUpdate.id, expectedHash = firmwareUpdate.sha256;
  bootId = "bridge-after-SD-activation";
  firmwareUpdate.id = "old-ESP-app-job"; firmwareUpdate.sha256 = "";
  firmwareUpdate.kind = "app"; firmwareUpdate.state = "idle";
  firmwareUpdating = false; octopusPhase = 0;
  assert(restoreOctopusUpdateReceipt());
  assert(firmwareUpdate.id == expectedId && firmwareUpdate.sha256 == expectedHash && firmwareUpdate.kind == "octopus");
  assert(octopusPhase == 5 && firmwareUpdating && !firmwareUpdateCancel);
  assert(Serial.sent == atActivation);
  localUart().session.state = timiac::OctopusSession::State::Ready; localUart().session.identity = true; localUart().session.lastRx = hostNow;
  localUart().sdOtaSupported = true; localUart().octopusSoftware = "older-installed-version";
  octopusTick(); assert(firmwareUpdating && octopusPhase == 5);
  localUart().octopusSoftware = kSoftwareRevision;
  octopusTick(); assert(firmwareUpdate.state == "complete" && !firmwareUpdating);
  bootId = "bridge-after-completed-receipt";
  firmwareUpdate.id = "";
  assert(restoreOctopusUpdateReceipt() && firmwareUpdate.id == expectedId);
  assert(firmwareUpdate.state == "complete" && !firmwareUpdating && !octopusPhase);
  const std::string committed = LittleFS.files["/octopus-receipt.json"];
  LittleFS.writeLimit = 20; assert(!saveOctopusUpdateReceipt(false));
  assert(LittleFS.files["/octopus-receipt.json"] == committed);
  LittleFS.writeLimit = std::numeric_limits<size_t>::max();
  LittleFS.files["/octopus-receipt.json"] = "{broken";
  assert(!restoreOctopusUpdateReceipt());
  LittleFS.files["/octopus-receipt.json"] = committed;
  // A failed durable write must prevent P3 entirely, while retaining the old
  // committed receipt even if its replacement could not be renamed.
  firmwareUpdating = true; firmwareUpdateCancel = false; octopusPhase = 3;
  LittleFS.failRename = true;
  Serial.feed("TIMIAC_FW_VERIFIED\n"); octopusTick();
  assert(firmwareUpdate.state == "failed" && !firmwareUpdating && !octopusPhase);
  assert(firmwareUpdate.error.indexOf("no activation requested") >= 0 && lastCommand() == "M997 P4");
  assert(LittleFS.files["/octopus-receipt.json"] == committed);
  LittleFS.failRename = false;
  firmwareUpdating = true; firmwareUpdateCancel = false; octopusPhase = 2;
  Serial.feed("Error:SD card unavailable\n"); octopusTick();
  assert(firmwareUpdate.state == "failed" && firmwareUpdate.error == "Error:SD card unavailable");
  // A native reset during a block must not be swallowed as another lost ACK.
  firmwareUpdating = true; firmwareUpdateCancel = false; octopusPhase = 2; octopusOffset = 512;
  const unsigned resetsBefore = localUart().resetCount;
  Serial.feed("start\n"); octopusTick();
  assert(!octopusPhase && !firmwareUpdating && localUart().resetCount == resetsBefore + 1);
  assert(firmwareUpdate.error.indexOf("Octopus restarted while writing SD block at byte 512") >= 0);
  // Give an exhausted block wait an actionable offset and actual last reply.
  firmwareUpdating = true; firmwareUpdateCancel = false; octopusPhase = 2;
  octopusCommand = first; octopusRetries = 0; octopusSent = hostNow;
  localUart().lastReply = "ok";
  for (unsigned retry = 0; retry < 5; ++retry) { hostNow += 10001; octopusTick(); }
  assert(!octopusPhase && !firmwareUpdating && firmwareUpdate.state == "failed");
  assert(firmwareUpdate.error.indexOf("writing SD block at 512/1024; last reply: ok") >= 0);
  std::cout << "Production Octopus sender: SD verification, one-shot activation, durable receipt across bridge reset, wrong-version rejection and interrupted receipt writes passed\n";
}
