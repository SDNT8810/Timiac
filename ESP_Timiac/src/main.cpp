#include <Arduino.h>
#include <ArduinoJson.h>
#include "platform_support.h"
#include <DNSServer.h>
#include <LittleFS.h>
#include "config.h"
#include "../../shared/safety.h"
#include "../../shared/timeouts.h"
#include "../../shared/gcode.h"
#include "../../shared/endstops.h"
#include "../../shared/driver_report.h"
#include "../../shared/clock.h"
#if TIMIAC_MASTER
#include "../../shared/presets.h"
#include "../../shared/proximity.h"
#include <Wire.h>
#include <Preferences.h>
#include <RTClib.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
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
#include "proximity_sensor.h"
#include "wifi_uplink.h"
#include "ntp_clock.h"
#include "cloud_relay.h"
#include "web_api.h"
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
  dns.start(53, "*", IPAddress(192,168,4,1));
  configureClock();
  rtcTick(true);
  configureProximity();
  LittleFS.begin(); configureHttp(); listener.begin();
#else
  WiFi.setSleepMode(WIFI_NONE_SLEEP);
  Serial.setRxBufferSize(2048);
  Serial.begin(kBaud); Serial.setDebugOutput(false);
  if (TIMIAC_SERIAL_SWAP) Serial.swap();
  serialLine.reserve(513);
  WiFi.mode(WIFI_STA); WiFi.hostname("timiac-board" + String(TIMIAC_BOARD_ID));
  WiFi.setAutoReconnect(true); WiFi.begin(TIMIAC_WIFI_SSID, TIMIAC_WIFI_PASSWORD);
#endif
  logMessage("Timiac three-ESP controller boot; no automatic homing or replay");
}
void loop() {
#if TIMIAC_MASTER
  {
    RuntimeLock lock;
    networkTick(); operationTick();
    dns.processNextRequest(); programTick(); uplinkTick(); ntpTick(); rtcTick(); proximityTick(); cloudTick();
  }
#else
  networkTick(); operationTick();
  uart.tick(); operationTick();
#endif
  yield();
}
