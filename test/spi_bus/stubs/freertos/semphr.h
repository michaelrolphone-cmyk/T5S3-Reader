#pragma once
#include "FreeRTOS.h"
#include <stdbool.h>
#include <stdlib.h>
/* Deterministic admission fixture, not a scheduler/concurrency model. */
typedef bool *SemaphoreHandle_t;
static inline SemaphoreHandle_t xSemaphoreCreateMutex(void) { return calloc(1,sizeof(bool)); }
static inline int xSemaphoreTake(SemaphoreHandle_t lock, TickType_t ticks) {
    (void)ticks;
    if (!lock || *lock) return 0;
    *lock=true; return 1;
}
static inline int xSemaphoreGive(SemaphoreHandle_t lock) { *lock=false; return 1; }
static inline void vSemaphoreDelete(SemaphoreHandle_t lock) { free(lock); }
