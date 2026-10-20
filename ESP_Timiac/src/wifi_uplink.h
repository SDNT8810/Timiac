#pragma once

Preferences wifiPreferences;
bool wifiStorageReady = false, wifiPending = false, wifiAttempted = false;
bool uplinkWasConnected = false;
String uplinkSsid, uplinkPassword, uplinkError;
uint32_t wifiAttemptAt = 0, wifiRetryDelay = 0;
uint32_t uplinkGeneration = 0, wifiAttempts = 0;

void configureUplink() {
  // loop() owns indefinite retries so the SDK cannot scan/change the hotspot
  // channel in the middle of motor motion or a local firmware update.
  WiFi.setAutoReconnect(false);
  wifiStorageReady = wifiPreferences.begin("timiac-wifi", false);
  if (!wifiStorageReady) { uplinkError = "Wi-Fi settings storage unavailable"; return; }
  DynamicJsonDocument saved(384);
  if (!deserializeJson(saved, wifiPreferences.getString("uplink", "{}"))) {
    uplinkSsid = saved["ssid"] | ""; uplinkPassword = saved["password"] | "";
  }
  wifiPending = !uplinkSsid.isEmpty();
}
const char* saveUplink(JsonVariant doc) {
  if (!wifiStorageReady) return "Wi-Fi settings storage unavailable";
  const bool forget = doc["forget"] | false;
  if (!forget && (!doc["ssid"].is<const char*>() || !doc["password"].is<const char*>())) return "Enter a Wi-Fi name and password";
  String ssid = forget ? "" : doc["ssid"].as<String>(), password = forget ? "" : doc["password"].as<String>();
  if (!forget && (ssid.isEmpty() || ssid.length() > 32 || password.length() > 63 || (password.length() && password.length() < 8)))
    return "Wi-Fi name must be 1-32 bytes; password must be empty or 8-63 characters";
  DynamicJsonDocument saved(384); saved["ssid"] = ssid; saved["password"] = password;
  String json; serializeJson(saved, json);
  if (!wifiPreferences.putString("uplink", json)) return "Could not save Wi-Fi settings";
  uplinkSsid = ssid; uplinkPassword = password; uplinkError = ""; wifiPending = true;
  return nullptr;
}
void uplinkTick() {
  // A local update must keep the hotspot stable even without an upstream
  // router. Do not scan/reconnect and change its channel during flashing.
  if (firmwareUpdating) return;
  // A paused/uploading program can remain reserved indefinitely. It must not
  // prevent router recovery when no motor step is active.
  const bool idle = !operation.barrier.active() && !program.hasStep && program.state != "running" && !stopPending && !stopRequested;
  const uint32_t now = millis();
  if (wifiPending && !idle) return;
  if (wifiPending) {
    WiFi.disconnect(false, false); networkChanging = false;
    uplinkWasConnected = false; wifiAttempted = false; wifiRetryDelay = 0; wifiPending = false;
  }
  if (uplinkSsid.isEmpty()) { networkChanging = false; return; }
  if (WiFi.status() == WL_CONNECTED) {
    const IPAddress ip = WiFi.localIP();
    if (ip[0] == 192 && ip[1] == 168 && ip[2] == 4) {
      uplinkError = "Router uses Timiac's 192.168.4.x subnet; change the router/hotspot subnet";
      WiFi.disconnect(false, false); networkChanging = false;
      uplinkWasConnected = false; wifiAttempted = true;
      wifiAttemptAt = now; wifiRetryDelay = 60000; return;
    }
    if (!uplinkWasConnected) { ++uplinkGeneration; logMessage("Router Wi-Fi connected; resuming cloud sync"); }
    uplinkWasConnected = true;
    networkChanging = false; wifiRetryDelay = 0; uplinkError = ""; return;
  }
  if (uplinkWasConnected) {
    uplinkWasConnected = false; wifiAttempted = false; wifiRetryDelay = 0;
    uplinkError = "Router disconnected; automatically reconnecting when motion is idle";
    logMessage(uplinkError);
  }
  if (networkChanging) {
    if (!timiac::expired(now, wifiAttemptAt, 15000)) return;
    WiFi.disconnect(false, false); networkChanging = false;
    uplinkError = "Could not join Wi-Fi; check name/password and 2.4 GHz availability. Retrying while idle.";
    wifiAttemptAt = now; wifiRetryDelay = wifiRetryDelay ? min(uint32_t(60000), wifiRetryDelay * 2) : 15000;
    return;
  }
  if (!idle || (wifiAttempted && !timiac::expired(now, wifiAttemptAt, wifiRetryDelay))) return;
  networkChanging = true; wifiAttempted = true; wifiAttemptAt = now; ++wifiAttempts;
  WiFi.begin(uplinkSsid.c_str(), uplinkPassword.c_str());
}
void writeUplinkStatus(JsonObject doc) {
  const bool connected = !wifiPending && WiFi.status() == WL_CONNECTED;
  doc["configured"] = !uplinkSsid.isEmpty(); doc["ssid"] = uplinkSsid;
  doc["connected"] = connected; doc["connecting"] = networkChanging; doc["pending"] = wifiPending;
  doc["ip"] = connected ? WiFi.localIP().toString() : "";
  doc["rssi"] = connected ? WiFi.RSSI() : 0;
  doc["apIp"] = "192.168.4.1"; doc["error"] = uplinkError;
  doc["attempts"] = wifiAttempts;
  doc["retryWhenIdle"] = !connected && !uplinkSsid.isEmpty() && !networkChanging;
  doc["retryInMs"] = !connected && wifiAttempted && millis() - wifiAttemptAt < wifiRetryDelay ? wifiRetryDelay - (millis() - wifiAttemptAt) : 0;
  // Never return the saved password. Router connectivity alone does not prove internet reachability.
}
