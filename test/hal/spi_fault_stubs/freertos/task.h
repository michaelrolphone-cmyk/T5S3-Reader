#pragma once
#include "FreeRTOS.h"
TickType_t xTaskGetTickCount();
TaskHandle_t xTaskGetCurrentTaskHandle();
void vTaskDelay(TickType_t);
void vTaskSuspend(TaskHandle_t);
