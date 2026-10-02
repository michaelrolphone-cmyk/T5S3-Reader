#pragma once
#include <cassert>
inline unsigned testStreamMutexDepth = 0;
using SemaphoreHandle_t = void*;
inline SemaphoreHandle_t xSemaphoreCreateMutex() { return reinterpret_cast<void*>(1); }
inline int xSemaphoreTake(SemaphoreHandle_t, unsigned) { assert(testStreamMutexDepth == 0); ++testStreamMutexDepth; return 1; }
inline void xSemaphoreGive(SemaphoreHandle_t) { assert(testStreamMutexDepth == 1); --testStreamMutexDepth; }
