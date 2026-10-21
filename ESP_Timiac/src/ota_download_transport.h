#pragma once
#include <freertos/queue.h>
#include <new>

// DNS, TCP, TLS and HTTP headers can block for many seconds. The worker owns
// this session until it returns it through the response mailbox. Only then may
// loop() read its body or close it. No hardware/runtime state is used here.
struct OtaDownloadSession {
  WiFiClientSecure tls;
  WiFiClient local;
  HTTPClient http;
  String url, error;
  bool localTransport = false;
  uint32_t expectedSize = 0;
  ~OtaDownloadSession() { http.end(); tls.stop(); local.stop(); }
};
QueueHandle_t otaOpenResponses = nullptr;
bool otaOpenInFlight = false;
void otaDownloadTask(void* argument) {
  auto* session = static_cast<OtaDownloadSession*>(argument);
  if (!session->localTransport) {
    session->tls.setCACert(strlen(TIMIAC_CLOUD_CA_CERT) ? TIMIAC_CLOUD_CA_CERT : kCloudRunRootCa);
    session->tls.setHandshakeTimeout(20);
  }
  session->http.setConnectTimeout(8000); session->http.setTimeout(5000);
  session->http.useHTTP10(true); session->http.setReuse(false);
  const bool opened = session->localTransport ? session->http.begin(session->local, session->url) : session->http.begin(session->tls, session->url);
  if (!opened) session->error = "Cannot open firmware download";
  else {
    if (!session->localTransport) session->http.addHeader("X-Timiac-Device-Key", TIMIAC_CLOUD_DEVICE_KEY);
    session->http.addHeader("Accept-Encoding", "identity");
    const int code = session->http.GET();
    if (code != 200) {
      session->error = String(session->localTransport ? "Local firmware server unreachable; allow Python through the PC firewall (" : "Firmware gateway connection failed (") + String(code) + ")";
      if (code < 0 && !session->localTransport) {
        char detail[128] = {};
        const int tlsError = session->tls.lastError(detail, sizeof(detail));
        if (tlsError) session->error += "; TLS " + String(tlsError) + ": " + detail;
      }
    }
    else if (session->http.getSize() != int(session->expectedSize)) session->error = "Firmware download length mismatch";
  }
  xQueueSend(otaOpenResponses, &session, portMAX_DELAY);
  // The receiver owns the session now. Return the 10 KB worker stack to
  // the heap instead of keeping an idle updater task for the entire boot.
  vTaskDelete(nullptr);
}
bool configureOtaDownloadTransport() {
  otaOpenResponses = xQueueCreate(1, sizeof(OtaDownloadSession*));
  return otaOpenResponses != nullptr;
}
bool startOtaDownload(OtaDownloadSession* session) {
  return otaOpenResponses && xTaskCreatePinnedToCore(otaDownloadTask, "timiac-ota", 10240, session, 1, nullptr, 0) == pdPASS;
}
