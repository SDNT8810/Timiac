#pragma once
#include "../../shared/machine_generated.h"

// All three modules must use the same credentials. Override in config.local.h.
#if __has_include("config.local.h")
#include "config.local.h"
#endif
#ifndef TIMIAC_RELEASE
#define TIMIAC_RELEASE 0
#endif
#ifndef TIMIAC_RTC_SDA
#define TIMIAC_RTC_SDA 21
#endif
#ifndef TIMIAC_RTC_SCL
#define TIMIAC_RTC_SCL 22
#endif
#ifndef TIMIAC_PROXIMITY_ENABLED
#define TIMIAC_PROXIMITY_ENABLED 1
#endif
#ifndef TIMIAC_SONAR_TRIG
#define TIMIAC_SONAR_TRIG 18
#endif
#ifndef TIMIAC_SONAR_ECHO
#define TIMIAC_SONAR_ECHO 19
#endif
#ifndef TIMIAC_WIFI_SSID
#define TIMIAC_WIFI_SSID "Timiac"
#endif
#ifndef TIMIAC_WIFI_PASSWORD
#define TIMIAC_WIFI_PASSWORD "12345678"
#endif
#ifndef TIMIAC_PEER_KEY
#define TIMIAC_PEER_KEY "timiac-pair-change-me"
#endif
// POSIX timezone syntax. This default keeps the DS3231 in Italy local time,
// including daylight-saving changes, after an internet time synchronization.
// Override it in config.local.h when the clock is installed elsewhere.
#ifndef TIMIAC_NTP_TIMEZONE
#define TIMIAC_NTP_TIMEZONE "CET-1CEST,M3.5.0/2,M10.5.0/3"
#endif
#ifndef TIMIAC_NTP_SERVER_1
#define TIMIAC_NTP_SERVER_1 "time.google.com"
#endif
#ifndef TIMIAC_NTP_SERVER_2
#define TIMIAC_NTP_SERVER_2 "pool.ntp.org"
#endif
// Leave these empty for local-only operation. config.local.h supplies the
// deployment URL and the per-device secret. A Google Trust root is built in for
// legacy Cloud Run endpoints; supply the Cloudflare trust bundle for its relay.
#ifndef TIMIAC_CLOUD_URL
#define TIMIAC_CLOUD_URL ""
#endif
#ifndef TIMIAC_CLOUD_DEVICE_ID
#define TIMIAC_CLOUD_DEVICE_ID ""
#endif
#ifndef TIMIAC_CLOUD_DEVICE_KEY
#define TIMIAC_CLOUD_DEVICE_KEY ""
#endif
#ifndef TIMIAC_CLOUD_CA_CERT
#define TIMIAC_CLOUD_CA_CERT ""
#endif

// UART0: TX GPIO1, RX GPIO3, suitable for the Octopus Wi-Fi socket.
// Set to 1 ONLY for externally wired TX GPIO15 / RX GPIO13.
#ifndef TIMIAC_SERIAL_SWAP
#define TIMIAC_SERIAL_SWAP 0
#endif
constexpr unsigned long kBaud = 115200;
// Application revision is separate from the hardware identity shared with Marlin.
constexpr char kSoftwareRevision[] = "2026-10-08.1";
// Checked against the compiled animation call chains before exporting a master.
constexpr unsigned kMasterLoopStackBytes = 8192;
// Wi-Fi/serial telemetry can be delayed while every axis is homing. These are
// link-loss limits, separate from the 180-second G28 completion limit.
constexpr unsigned long kPeerTimeout = 15000;
constexpr unsigned long kHealthTimeout = 15000;
constexpr unsigned long kPrepareTimeout = 15000;
constexpr unsigned long kLeaseMs = 15000;
constexpr unsigned long kHeartbeatMs = 250;
// Each Octopus runs X/Y/Z/A/B/C. All three ESP modules must use this same count.
constexpr unsigned kAxisCount = timiac::axisCount;
constexpr uint8_t kHomedMask = (1U << kAxisCount) - 1;
constexpr float kTravelMm = timiac::travelMm;
constexpr float kMaxFeed = timiac::maxFeed;
constexpr unsigned kMaxLine = 160;
constexpr unsigned kMaxCommands = 6;
constexpr unsigned kPeerPort = 8266;
// Local G-code letters on both the peer link and Octopus UART. Marlin's
// internal I/J/K driver identifiers use AXIS4/5/6_NAME = A/B/C in G-code.
constexpr auto& kAxes = timiac::axes;
