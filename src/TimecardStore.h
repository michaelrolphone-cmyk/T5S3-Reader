#pragma once

#include <cstdint>
#include <string>
#include <vector>

enum class TimecardPunch : uint8_t { ClockIn, LunchStart, LunchEnd, ClockOut, Count };

struct TimecardDay {
  int ymd = 0;
  int16_t punches[4] = {-1, -1, -1, -1};

  int16_t get(TimecardPunch punch) const { return punches[static_cast<int>(punch)]; }
  void set(TimecardPunch punch, int16_t minutes) { punches[static_cast<int>(punch)] = minutes; }
  bool hasAny() const {
    for (int i = 0; i < 4; ++i) {
      if (punches[i] >= 0) {
        return true;
      }
    }
    return false;
  }
};

class TimecardStore {
  static TimecardStore instance;
  std::vector<TimecardDay> days;

 public:
  static TimecardStore& getInstance() { return instance; }

  void loadFromFile();
  bool saveToFile() const;

  TimecardDay getDay(int ymd) const;
  void setPunch(int ymd, TimecardPunch punch, int16_t minutesFromMidnight);
  void clearPunch(int ymd, TimecardPunch punch);
};

#define TIMECARD TimecardStore::getInstance()

namespace TimecardTime {
int todayYmd();
int currentMinutes();
int sundayYmd(int weekOffset);
int addDays(int ymd, int days);
int weekOfYear(int ymd);
void splitYmd(int ymd, int& year, int& month, int& day);
const char* monthAbbrev(int month);
const char* weekdayAbbrev(int ymd);
std::string formatAmpm(int16_t minutes);
bool parseAmpm(const char* text, int16_t& minutesOut);
std::string formatWeekLabel(int sundayYmd);
int16_t workedMinutes(const TimecardDay& day);
}  // namespace TimecardTime
