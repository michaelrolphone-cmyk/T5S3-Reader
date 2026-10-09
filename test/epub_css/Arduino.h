#pragma once
#include <cstdint>
uint32_t millis();
struct EspFixture { uint32_t getFreeHeap() const; };
extern EspFixture ESP;
