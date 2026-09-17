#pragma once
#include "freertos/FreeRTOS.h"
TaskHandle_t test_current_task(void);
#define xTaskGetCurrentTaskHandle() test_current_task()
