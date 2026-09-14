#pragma once
struct StaticSemaphore_t {};
using SemaphoreHandle_t = StaticSemaphore_t*;
inline SemaphoreHandle_t xSemaphoreCreateMutexStatic(StaticSemaphore_t* storage) { return storage; }
inline int xSemaphoreTake(SemaphoreHandle_t, unsigned) { return 1; }
inline void xSemaphoreGive(SemaphoreHandle_t) {}
