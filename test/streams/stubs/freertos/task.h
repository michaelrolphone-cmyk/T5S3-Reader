#pragma once
#include <cstring>
using TaskHandle_t = void*;
extern void (*httpTask)(void*);
extern void* httpContext;
inline int xTaskCreate(void (*fn)(void*), const char* name, unsigned, void* ctx, unsigned, TaskHandle_t* out) {
  if (!std::strcmp(name, "stream-http")) { httpTask = fn; httpContext = ctx; }
  if (out) *out = reinterpret_cast<void*>(1);
  return 1;
}
inline unsigned ulTaskNotifyTake(int, unsigned) { return 0; }
inline void xTaskNotifyGive(TaskHandle_t) {}
inline void vTaskDelete(void*) {}

inline void vTaskDelay(unsigned) {}
