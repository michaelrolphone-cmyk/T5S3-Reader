#pragma once
#include <cstdint>
using TickType_t=uint32_t;
#ifndef pdMS_TO_TICKS
#define pdMS_TO_TICKS(ms) (ms)
#endif
