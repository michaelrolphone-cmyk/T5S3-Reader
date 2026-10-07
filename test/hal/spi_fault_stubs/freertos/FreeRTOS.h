#pragma once
#include <cstdint>
using TickType_t = uint32_t;
using TaskHandle_t = void*;
using SemaphoreHandle_t = void*;
using portMUX_TYPE = int;
#define portMUX_INITIALIZER_UNLOCKED 0
#define pdTRUE 1
#define pdMS_TO_TICKS(n) (n)
#define portENTER_CRITICAL(m) ((void)(m))
#define portEXIT_CRITICAL(m) ((void)(m))
