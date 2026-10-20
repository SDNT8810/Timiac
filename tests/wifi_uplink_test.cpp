#define main coordinatorRegressionMain
#include "coordinator_test.cpp"
#undef main
using std::min;
#include "../ESP_Timiac/src/wifi_uplink.h"

void save(const char* json) {
  DynamicJsonDocument command(384); assert(!deserializeJson(command, json));
  assert(!saveUplink(command.as<JsonVariant>()));
}
void joined() {
  WiFi.state = WL_CONNECTED; uplinkTick();
  assert(!networkChanging && !wifiPending && uplinkError.isEmpty());
}
int main() {
  resetMaster();
  wifiPreferences.savedString = "{\"ssid\":\"Studio\",\"password\":\"test-password\"}";
  WiFi.state = 6; configureUplink();
  assert(wifiPending && !WiFi.autoReconnect);
  uplinkTick(); assert(networkChanging && WiFi.attempts == 1 && WiFi.ssid == "Studio");
  joined(); assert(uplinkGeneration == 1);
  // Router reboot during motion: preserve peer safety, then retry as soon as
  // motion finishes. No password re-entry or manual forget is involved.
  operation.barrier.phase = timiac::Phase::Running; WiFi.state = 6;
  uplinkTick(); assert(!networkChanging && WiFi.attempts == 1);
  operation.barrier.phase = timiac::Phase::Done;
  uplinkTick(); assert(networkChanging && WiFi.attempts == 2);
  // Keep the router down for >10 minutes: bounded backoff never gives up and
  // never scans continuously or clears the saved credentials.
  for (unsigned n = 0; n < 12; ++n) {
    hostNow += 15000; uplinkTick();
    assert(!networkChanging && wifiRetryDelay >= 15000 && wifiRetryDelay <= 60000);
    const unsigned attempts = WiFi.attempts;
    hostNow += wifiRetryDelay - 1; uplinkTick(); assert(WiFi.attempts == attempts);
    ++hostNow; uplinkTick(); assert(networkChanging && WiFi.attempts == attempts + 1);
    assert(WiFi.ssid == "Studio" && WiFi.password == "test-password");
  }
  joined(); assert(uplinkGeneration == 2);
  // Paused programs previously suppressed retries forever.
  program.state = "paused"; program.hasStep = false; WiFi.state = 6;
  uplinkTick(); assert(networkChanging); joined();
  firmwareUpdating = true; WiFi.state = 6;
  const unsigned attempts = WiFi.attempts;
  hostNow += 120000; uplinkTick(); assert(WiFi.attempts == attempts);
  firmwareUpdating = false; uplinkTick(); assert(WiFi.attempts == attempts + 1); joined();
  // Reboot loads the persisted network again. Forget really stops all retries.
  configureUplink(); assert(wifiPending && uplinkSsid == "Studio"); uplinkTick(); joined();
  save("{\"forget\":true}"); uplinkTick();
  assert(uplinkSsid.isEmpty() && !networkChanging && WiFi.state != WL_CONNECTED);
  const unsigned forgottenAttempts = WiFi.attempts;
  hostNow += 3600000; uplinkTick(); assert(WiFi.attempts == forgottenAttempts);
  DynamicJsonDocument status(1024); writeUplinkStatus(status.to<JsonObject>());
  assert(!status.containsKey("password"));
  // Wrap-safe timing after a long uptime.
  hostNow = UINT32_MAX - 1000; save("{\"ssid\":\"Studio\",\"password\":\"test-password\"}");
  uplinkTick(); hostNow += 15000; uplinkTick(); assert(!networkChanging);
  hostNow += wifiRetryDelay; uplinkTick(); assert(networkChanging); joined();
  std::cout << "Saved Wi-Fi: router reboot, indefinite backoff, paused programs, update exclusion, persistence, forget and timer wrap passed\n";
}
