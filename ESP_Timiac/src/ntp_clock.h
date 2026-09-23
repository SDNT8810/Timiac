#pragma once

// The DS3231 remains the source used by movements and the dashboard. NTP only
// corrects that clock after the station Wi-Fi has a usable upstream connection.
bool ntpWaiting = false, ntpWasOnline = false;
String ntpState = "waiting for router", ntpError, ntpLastSync;
uint32_t ntpStartedAt = 0, ntpLastAttemptAt = 0, ntpLastSyncAt = 0;

constexpr uint32_t kNtpAttemptTimeout = 45000;
constexpr uint32_t kNtpRetryInterval = 60000;
constexpr uint32_t kNtpSyncInterval = 12UL * 60UL * 60UL * 1000UL;
constexpr time_t kNtpEarliestEpoch = 1704067200; // 2024-01-01 UTC

bool ntpCanRun() {
  return !operation.barrier.active() && !program.reserved() && !stopPending && !networkChanging;
}
void startNtpSync(uint32_t now) {
  configTzTime(TIMIAC_NTP_TIMEZONE, TIMIAC_NTP_SERVER_1, TIMIAC_NTP_SERVER_2);
  ntpWaiting = true; ntpStartedAt = ntpLastAttemptAt = now;
  ntpState = "synchronizing"; ntpError = "";
}
void writeNtpStatus(JsonObject clock) {
  clock["timezone"] = TIMIAC_NTP_TIMEZONE;
  clock["internetSync"] = ntpState;
  clock["internetError"] = ntpError;
  clock["internetLastSync"] = ntpLastSync;
  clock["internetWaiting"] = ntpWaiting;
}
void ntpTick() {
  const uint32_t now = millis();
  const bool online = !uplinkSsid.isEmpty() && !wifiPending && WiFi.status() == WL_CONNECTED;
  if (!online) {
    ntpWasOnline = false;
    if (!uplinkSsid.isEmpty()) ntpState = networkChanging ? "waiting for router" : "router unavailable";
    else ntpState = "router not configured";
    return;
  }
  const bool connectionChanged = !ntpWasOnline;
  ntpWasOnline = true;
  const bool due = !ntpLastSyncAt || timiac::expired(now, ntpLastSyncAt, kNtpSyncInterval);
  if (!ntpWaiting && ntpCanRun() && (connectionChanged || due) &&
      (!ntpLastAttemptAt || timiac::expired(now, ntpLastAttemptAt, kNtpRetryInterval))) {
    startNtpSync(now);
  }
  if (!ntpWaiting) return;
  const time_t epoch = time(nullptr);
  if (epoch >= kNtpEarliestEpoch) {
    struct tm local {};
    localtime_r(&epoch, &local);
    const DateTime date(local.tm_year + 1900, local.tm_mon + 1, local.tm_mday,
                        local.tm_hour, local.tm_min, local.tm_sec);
    if (!date.isValid() || !setRtcDate(date)) {
      ntpWaiting = false; ntpState = "sync failed";
      ntpError = rtcError.isEmpty() ? "Could not write internet time to DS3231" : rtcError;
      return;
    }
    ntpWaiting = false; ntpLastSyncAt = now; ntpState = "synchronized"; ntpError = "";
    ntpLastSync = rtcText;
    logMessage("RTC synchronized from internet time");
    return;
  }
  if (timiac::expired(now, ntpStartedAt, kNtpAttemptTimeout)) {
    ntpWaiting = false; ntpState = "sync failed";
    ntpError = "NTP server did not respond; retrying while idle";
  }
}
