#pragma once
#include <cstdint>
namespace RadioPower {
enum class Owner : uint8_t { Gps = 1, LoRa = 2 };
bool acquire(Owner owner);
void release(Owner owner);
}
