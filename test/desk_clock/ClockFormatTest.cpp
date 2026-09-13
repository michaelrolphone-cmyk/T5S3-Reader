#include <ClockFormat.h>

#include <cassert>
#include <cstring>

int main() {
  struct Example {
    uint8_t hour;
    uint8_t minute;
    const char* twelve;
    const char* twentyFour;
  };
  const Example examples[] = {
      {0, 0, "12:00 AM", "00:00"}, {0, 1, "12:01 AM", "00:01"},
      {1, 9, "1:09 AM", "01:09"}, {11, 59, "11:59 AM", "11:59"},
      {12, 0, "12:00 PM", "12:00"}, {12, 1, "12:01 PM", "12:01"},
      {13, 5, "1:05 PM", "13:05"}, {23, 59, "11:59 PM", "23:59"},
  };
  char buffer[ClockFormat::BUFFER_SIZE];
  for (const auto& example : examples) {
    assert(ClockFormat::format(buffer, sizeof(buffer), example.hour, example.minute, true));
    assert(std::strcmp(buffer, example.twelve) == 0);
    assert(ClockFormat::format(buffer, sizeof(buffer), example.hour, example.minute, false));
    assert(std::strcmp(buffer, example.twentyFour) == 0);
  }
  // Every minute must fit the UI buffers, with an unambiguous period in 12h mode.
  for (unsigned hour = 0; hour < 24; ++hour) {
    for (unsigned minute = 0; minute < 60; ++minute) {
      assert(ClockFormat::format(buffer, sizeof(buffer), hour, minute, true));
      assert(std::strlen(buffer) >= 7 && std::strlen(buffer) <= 8);
      assert(std::strcmp(buffer + std::strlen(buffer) - 2, hour < 12 ? "AM" : "PM") == 0);
      assert(ClockFormat::displayHour(hour, true) >= 1 && ClockFormat::displayHour(hour, true) <= 12);
      assert(ClockFormat::format(buffer, sizeof(buffer), hour, minute, false));
      assert(std::strlen(buffer) == 5);
      assert(ClockFormat::displayHour(hour, false) == hour);
    }
  }
  char shortBuffer[6] = {};
  assert(!ClockFormat::format(shortBuffer, sizeof(shortBuffer), 12, 0, true));
  assert(shortBuffer[0] == '\0');  // Never display a truncated, ambiguous clock.
  assert(ClockFormat::format(shortBuffer, sizeof(shortBuffer), 12, 0, false));
  assert(!ClockFormat::format(nullptr, 9, 0, 0, true));
  assert(!ClockFormat::format(buffer, 0, 0, 0, true));
  assert(!ClockFormat::format(buffer, sizeof(buffer), 24, 0, true));
  assert(!ClockFormat::format(buffer, sizeof(buffer), 0, 60, false));
}
