#pragma once
#include "FreeRTOS.h"
TaskHandle_t xTaskGetCurrentTaskHandle(void);
TickType_t xTaskGetTickCount(void);
void vTaskDelay(TickType_t ticks);
void vTaskDelete(TaskHandle_t task);
int xTaskCreatePinnedToCore(void (*entry)(void*), const char *name,
 uint32_t stack,void *argument,uint32_t priority,TaskHandle_t *out,int core);
