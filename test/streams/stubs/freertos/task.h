#pragma once
#include <cstring>

using TaskHandle_t = void*;
extern void (*httpTask)(void*);
extern void* httpContext;

namespace test_freertos {
struct TaskBlocked {};
inline void (*httpWorker)(void*) = nullptr;
inline bool httpNotified = false;

inline void runHttpWorkerOnce(void* context) {
  try {
    if (httpWorker) httpWorker(context);
  } catch (const TaskBlocked&) {
    // A persistent firmware task blocks here waiting for its next notification.
    // Returning from the host shim represents yielding back to the test runner.
  }
}
}  // namespace test_freertos

inline int xTaskCreate(void (*fn)(void*), const char* name, unsigned, void* ctx,
                       unsigned, TaskHandle_t* out) {
  if (!std::strcmp(name, "stream-http")) {
    test_freertos::httpWorker = fn;
    httpTask = test_freertos::runHttpWorkerOnce;
    httpContext = ctx;
  }
  if (out) *out = reinterpret_cast<void*>(1);
  return 1;
}

inline unsigned ulTaskNotifyTake(int, unsigned) {
  if (test_freertos::httpNotified) {
    test_freertos::httpNotified = false;
    return 1;
  }
  throw test_freertos::TaskBlocked{};
}

inline void xTaskNotifyGive(TaskHandle_t) { test_freertos::httpNotified = true; }
inline void vTaskDelete(void*) {}
