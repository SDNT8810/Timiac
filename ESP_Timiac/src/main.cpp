#include <Arduino.h>
#include <ArduinoJson.h>
#include "platform_support.h"
#include <DNSServer.h>
#include <LittleFS.h>
#include "config.h"
#include "../../shared/safety.h"
#include "../../shared/peer_outbox.h"
#include "../../shared/timeouts.h"
#include "../../shared/gcode.h"
#include "../../shared/firmware_transfer.h"
#include "../../shared/endstops.h"
#include "../../shared/driver_report.h"
#include "../../shared/octopus_session.h"
#include "../../shared/clock.h"
#if TIMIAC_MASTER
#include "../../shared/presets.h"
#include "../../shared/proximity.h"
#include <Wire.h>
#include <Preferences.h>
#include <RTClib.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <Update.h>
#include <mbedtls/sha256.h>
#include <mbedtls/md.h>
SET_LOOP_TASK_STACK_SIZE(kMasterLoopStackBytes);
#else
#include <Updater.h>
#include <bearssl/bearssl.h>
#endif

namespace {
#if TIMIAC_MASTER
AsyncWebServer http(80);
DNSServer dns;
#endif
WiFiServer listener(kPeerPort);
#include "runtime.h"
#include "uart_bridge.h"
#include "peer_link.h"
#if TIMIAC_MASTER
#include "rtc_clock.h"
#include "builtin_program.h"
#include "program_runner.h"
#include "proximity_sensor.h"
#include "wifi_uplink.h"
#include "ntp_clock.h"
#include "motion_speed.h"
#include "firmware_update.h"
#include "cloud_relay.h"
#include "web_api.h"
#include "release_clock.h"
#else
#include "firmware_update.h"
#endif
}

void setup() {
  bootId = platformChipId() + "-" + String(platformRandom(), HEX);
  WiFi.persistent(false);
#if TIMIAC_MASTER
  runtimeMutex = xSemaphoreCreateRecursiveMutex();
  configASSERT(runtimeMutex);
  RuntimeLock lock;
  WiFi.mode(WIFI_AP_STA);
  WiFi.setSleep(false);
  WiFi.softAPConfig(IPAddress(192,168,4,1), IPAddress(192,168,4,1), IPAddress(255,255,255,0));
  WiFi.softAP(TIMIAC_WIFI_SSID, TIMIAC_WIFI_PASSWORD, 6, false, 8);
  configureUplink();
  configureMotionSpeed();
  dns.start(53, "*", IPAddress(192,168,4,1));
  configureClock();
  rtcTick(true);
  configureProximity();
  LittleFS.begin(); configureFirmwareUpdate(); configureHttp(); listener.begin();
#else
  bridgeResetReason = ESP.getResetReason();
  WiFi.setSleepMode(WIFI_NONE_SLEEP);
  Serial.setRxBufferSize(2048);
  Serial.begin(kBaud); Serial.setDebugOutput(false);
  if (TIMIAC_SERIAL_SWAP) Serial.swap();
  serialLine.reserve(513);
  WiFi.mode(WIFI_STA); WiFi.hostname("timiac-board" + String(TIMIAC_BOARD_ID));
  WiFi.config(IPAddress(192,168,4,240 + TIMIAC_BOARD_ID), IPAddress(192,168,4,1), IPAddress(255,255,255,0));
  WiFi.setAutoReconnect(true); WiFi.begin(TIMIAC_WIFI_SSID, TIMIAC_WIFI_PASSWORD);
  configureFirmwareUpdate();
#endif
#if TIMIAC_MASTER && TIMIAC_RELEASE
  logMessage("Timiac release boot; waiting for both boards, then automatic Home all and clock");
#else
  logMessage("Timiac three-ESP controller boot; no automatic homing or replay");
#endif
}
void loop() {
#if TIMIAC_MASTER
  {
    RuntimeLock lock;
    networkTick(); operationTick();
    dns.processNextRequest(); programTick(); uplinkTick(); ntpTick(); rtcTick(); releaseClockTick(); proximityTick(); cloudTick(); firmwareUpdateTick();
  }
#else
  networkTick(); operationTick();
  if (!firmwareUpdateOwnsUart()) uart.tick();
  firmwareUpdateTick(); operationTick();
#endif
  yield();
}
