#pragma once
#include <cstdint>
namespace Board {
inline void beginI2C() {}
inline void beginBatteryManagement() {}
inline bool readBatteryStateOfCharge(uint16_t*) { return false; }
inline const char* displayName() { return "test"; }
struct GT911Touch { bool isAvailable() const { return false; } };
}
