#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdio>

// Presentation only: RTC storage, timezone conversion and timers stay unchanged.
namespace ClockFormat {
constexpr size_t BUFFER_SIZE = 9;  // "12:59 PM" plus terminator

constexpr unsigned displayHour(unsigned hour, bool use12Hour) {
  return use12Hour ? (hour % 12 == 0 ? 12 : hour % 12) : hour;
}

constexpr const char* period(unsigned hour) { return hour < 12 ? "AM" : "PM"; }

inline bool format(char* buffer, size_t size, uint8_t hour, uint8_t minute, bool use12Hour) {
  if (buffer == nullptr || size == 0) return false;
  buffer[0] = '\0';
  if (hour > 23 || minute > 59) return false;
  const int length = use12Hour
                         ? snprintf(buffer, size, "%u:%02u %s", displayHour(hour, true),
                                    static_cast<unsigned>(minute), period(hour))
                         : snprintf(buffer, size, "%02u:%02u", static_cast<unsigned>(hour),
                                    static_cast<unsigned>(minute));
  if (length < 0 || static_cast<size_t>(length) >= size) {
    buffer[0] = '\0';
    return false;
  }
  return true;
}
}  // namespace ClockFormat
