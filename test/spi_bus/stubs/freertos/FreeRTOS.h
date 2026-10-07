#pragma once
#include <stdint.h>
#include <stddef.h>
typedef uint32_t TickType_t;
#define pdTRUE 1
#define pdMS_TO_TICKS(ms) ((TickType_t)(ms))
