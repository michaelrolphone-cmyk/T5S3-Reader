#pragma once
#include <HalStorage.h>
inline void vTaskDelay(unsigned ticks) { ++yields; clockMs+=ticks; }
