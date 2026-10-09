#pragma once
#include <cstdint>
using TickType_t = uint32_t;
#define pdMS_TO_TICKS(value) (static_cast<TickType_t>(value))
