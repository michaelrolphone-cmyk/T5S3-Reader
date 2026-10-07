#pragma once
#include <cstdint>
#include <chrono>
#include <thread>
inline uint32_t millis() { return (uint32_t)std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count(); }
inline void delay(unsigned n) { std::this_thread::sleep_for(std::chrono::milliseconds(n)); }
