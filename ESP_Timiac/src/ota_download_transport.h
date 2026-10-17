#pragma once
#include <freertos/queue.h>
#include <new>

// DNS, TCP, TLS and HTTP headers can block for many seconds. The worker owns
// this session until it returns it through the response mailbox. Only then may
// loop() read its body or close it. No hardware/runtime state is used here.
struct OtaDownloadSession {
  WiFiClientSecure tls;
  HTTPClient http;
  String url, error;
  uint32_t expectedSize = 0;
  ~OtaDownloadSession() { http.end(); tls.stop(); }
};
QueueHandle_t otaOpenRequests = nullptr, otaOpenResponses = nullptr;
TaskHandle_t otaOpenTask = nullptr;
bool otaOpenInFlight = false;
void otaDownloadTask(void*) {
  for (;;) {
    OtaDownloadSession* session = nullptr;
    if (xQueueReceive(otaOpenRequests, &session, portMAX_DELAY) != pdTRUE) continue;
    session->tls.setCACert(strlen(TIMIAC_CLOUD_CA_CERT) ? TIMIAC_CLOUD_CA_CERT : kCloudRunRootCa);
    session->tls.setHandshakeTimeout(20);
    session->http.setConnectTimeout(8000); session->http.setTimeout(5000);
    session->http.useHTTP10(true); session->http.setReuse(false);
    if (!session->http.begin(session->tls, session->url)) session->error = "Cannot open firmware download";
    else {
      session->http.addHeader("X-Timiac-Device-Key", TIMIAC_CLOUD_DEVICE_KEY);
      session->http.addHeader("Accept-Encoding", "identity");
      const int code = session->http.GET();
      if (code != 200) {
        session->error = "Firmware gateway connection failed (" + String(code) + ")";
        if (code < 0) {
          char detail[128] = {};
          const int tlsError = session->tls.lastError(detail, sizeof(detail));
          if (tlsError) session->error += "; TLS " + String(tlsError) + ": " + detail;
        }
      }
      else if (session->http.getSize() != int(session->expectedSize)) session->error = "Firmware download length mismatch";
    }
    xQueueSend(otaOpenResponses, &session, portMAX_DELAY);
  }
}
bool configureOtaDownloadTransport() {
  otaOpenRequests = xQueueCreate(1, sizeof(OtaDownloadSession*));
  otaOpenResponses = xQueueCreate(1, sizeof(OtaDownloadSession*));
  if (otaOpenRequests && otaOpenResponses && xTaskCreatePinnedToCore(otaDownloadTask, "timiac-ota", 12288, nullptr, 1, &otaOpenTask, 0) == pdPASS) return true;
  if (otaOpenRequests) vQueueDelete(otaOpenRequests);
  if (otaOpenResponses) vQueueDelete(otaOpenResponses);
  otaOpenRequests = otaOpenResponses = nullptr; return false;
}
