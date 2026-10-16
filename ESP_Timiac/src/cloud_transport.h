#pragma once
#include <freertos/queue.h>
#include <new>

// Only serialized messages cross the task boundary. The worker never touches
// motors, peers, the clock, OTA state, or the runtime mutex. DNS/TLS may block;
// the real-time coordinator must continue to service its 250 ms heartbeats.
struct CloudExchange {
  String payload, response, error;
  uint32_t elapsed = 0;
};
QueueHandle_t cloudRequests = nullptr, cloudResponses = nullptr;
bool cloudInFlight = false;
TaskHandle_t cloudTask = nullptr;

void performCloudExchange(CloudExchange& exchange) {
  const uint32_t started = millis();
  WiFiClientSecure client;
  client.setCACert(strlen(TIMIAC_CLOUD_CA_CERT) ? TIMIAC_CLOUD_CA_CERT : kCloudRunRootCa);
  client.setHandshakeTimeout(3); // Seconds; the SDK otherwise allows 120 seconds.
  HTTPClient request;
  request.setConnectTimeout(1500); request.setTimeout(1500);
  const String endpoint = String(TIMIAC_CLOUD_URL) + "/device/v1/devices/" + TIMIAC_CLOUD_DEVICE_ID + "/sync";
  if (!request.begin(client, endpoint)) { exchange.error = "Could not start secure cloud connection"; return; }
  request.addHeader("Content-Type", "application/json");
  request.addHeader("X-Timiac-Device-Key", TIMIAC_CLOUD_DEVICE_KEY);
  const int code = request.POST(exchange.payload);
  exchange.payload = "";
  if (code != 200) exchange.error = "Cloud service returned HTTP " + String(code);
  else {
    DynamicJsonDocument inbound(2048);
    if (deserializeJson(inbound, request.getStream())) exchange.error = "Invalid cloud response";
    else serializeJson(inbound, exchange.response);
  }
  request.end();
  exchange.elapsed = millis() - started;
}
void cloudTransportTask(void*) {
  for (;;) {
    CloudExchange* exchange = nullptr;
    if (xQueueReceive(cloudRequests, &exchange, portMAX_DELAY) != pdTRUE) continue;
    performCloudExchange(*exchange);
    // HTTP/TLS resources have already been freed before handing ownership back.
    xQueueSend(cloudResponses, &exchange, portMAX_DELAY);
  }
}
bool configureCloudTransport() {
  cloudRequests = xQueueCreate(1, sizeof(CloudExchange*));
  cloudResponses = xQueueCreate(1, sizeof(CloudExchange*));
  if (cloudRequests && cloudResponses && xTaskCreatePinnedToCore(cloudTransportTask, "timiac-cloud", 12288, nullptr, 1, &cloudTask, 0) == pdPASS) return true;
  if (cloudRequests) vQueueDelete(cloudRequests);
  if (cloudResponses) vQueueDelete(cloudResponses);
  cloudRequests = cloudResponses = nullptr;
  return false;
}
