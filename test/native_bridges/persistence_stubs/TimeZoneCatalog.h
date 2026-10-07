#pragma once
#include <cstddef>
#include <cstdint>
enum class TimeZoneRegion : uint8_t { UTC = 0, America = 1, Europe = 2, Count = 3 };
struct TimeZoneEntry { const char* id; const char* posix; TimeZoneRegion region; };
namespace TimeZoneCatalog {
const TimeZoneEntry* data();
uint16_t count();
TimeZoneRegion regionOf(const char* id);
const char* regionDisplayName(TimeZoneRegion region);
void formatDisplayName(const char* id, char* buffer, size_t capacity);
void copyId(char* destination, size_t capacity, const char* id);
}
