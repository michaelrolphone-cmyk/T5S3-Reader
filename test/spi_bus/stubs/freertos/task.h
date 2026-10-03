#pragma once
#include "FreeRTOS.h"
typedef void *TaskHandle_t;
extern TickType_t fixture_ticks;
extern TaskHandle_t fixture_task;
static inline TickType_t xTaskGetTickCount(void) { return fixture_ticks; }
static inline TaskHandle_t xTaskGetCurrentTaskHandle(void) { return fixture_task; }
