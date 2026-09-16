#pragma once
#include <cstdint>
using TickType_t = uint32_t;
#define portMAX_DELAY UINT32_MAX
#define pdTRUE 1
#define pdPASS 1
#define pdMS_TO_TICKS(x) (x)
