#pragma once
#include <stdint.h>
inline uint32_t readerClock=0,readerTick=1;
inline uint32_t millis(){ return readerClock; }
