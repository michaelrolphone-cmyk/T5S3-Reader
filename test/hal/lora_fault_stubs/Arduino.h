#pragma once
#include <cstdint>
#define IRAM_ATTR
#define OUTPUT 1
#define INPUT 0
#define HIGH 1
#define LOW 0
extern unsigned hardwareCalls;
inline void pinMode(int,int) { ++hardwareCalls; }
inline void digitalWrite(int,int) { ++hardwareCalls; }
inline void delay(unsigned) {}
inline uint32_t millis() { return 1; }
