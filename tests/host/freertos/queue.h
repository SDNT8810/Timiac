#pragma once
// Threaded host implementation of the one-slot FreeRTOS mailboxes used by the
// production cloud worker. Tests exercise actual ownership and blocking waits.
#include <mutex>
#include <condition_variable>
#include <thread>
#include <cstring>
#include <cstdint>
constexpr unsigned pdTRUE = 1, pdPASS = 1, portMAX_DELAY = UINT32_MAX;
struct HostQueueClosed {};
struct HostQueue {
  std::mutex mutex;
  std::condition_variable ready;
  void* item = nullptr;
  bool closed = false;
};
using QueueHandle_t = HostQueue*;
using TaskHandle_t = std::thread*;
inline QueueHandle_t xQueueCreate(unsigned count, size_t size) { return count == 1 && size == sizeof(void*) ? new HostQueue : nullptr; }
inline unsigned xQueueSend(HostQueue* q, const void* source, unsigned wait) {
  std::unique_lock<std::mutex> lock(q->mutex);
  if (wait == portMAX_DELAY) q->ready.wait(lock, [&] { return !q->item || q->closed; });
  if (q->closed) throw HostQueueClosed();
  if (q->item) return 0;
  std::memcpy(&q->item, source, sizeof(void*)); q->ready.notify_all(); return pdTRUE;
}
inline unsigned xQueueReceive(HostQueue* q, void* destination, unsigned wait) {
  std::unique_lock<std::mutex> lock(q->mutex);
  if (wait == portMAX_DELAY) q->ready.wait(lock, [&] { return q->item || q->closed; });
  if (q->closed) throw HostQueueClosed();
  if (!q->item) return 0;
  std::memcpy(destination, &q->item, sizeof(void*)); q->item = nullptr; q->ready.notify_all(); return pdTRUE;
}
inline void vQueueDelete(HostQueue* q) { delete q; }
inline unsigned xTaskCreatePinnedToCore(void (*task)(void*), const char*, unsigned stack, void* arg, unsigned priority, TaskHandle_t* handle, unsigned core) {
  if (stack < 8192 || priority != 1 || core != 0) return 0;
  *handle = new std::thread([=] { try { task(arg); } catch (const HostQueueClosed&) {} }); return pdPASS;
}
