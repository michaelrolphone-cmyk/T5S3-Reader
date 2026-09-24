#pragma once
#include <cassert>
using SemaphoreHandle_t = bool*;
constexpr unsigned portMAX_DELAY = ~0u;
inline SemaphoreHandle_t xSemaphoreCreateMutex() { static bool held = false; return &held; }
inline void xSemaphoreTake(SemaphoreHandle_t mutex, unsigned) { assert(!*mutex); *mutex = true; }
inline void xSemaphoreGive(SemaphoreHandle_t mutex) { assert(*mutex); *mutex = false; }
