#include "TimecardStore.h"

#include <ArduinoJson.h>
#include <HalStorage.h>
#include <Logging.h>

#include <cctype>
#include <cstdio>
#include <cstring>
#include <ctime>

namespace {
constexpr char kPath[] = "/.crosspoint/timecard.json";
constexpr int kMaxDays = 400;

int clampMinutes(int minutes) {
  if (minutes < 0) {
    return -1;
  }
  if (minutes > 23 * 60 + 59) {
    return 23 * 60 + 59;
  }
  return minutes;
}
}  // namespace

TimecardStore TimecardStore::instance;

void TimecardStore::loadFromFile() {
  days.clear();
  if (!Storage.exists(kPath)) {
    return;
  }
  const String json = Storage.readFile(kPath);
  if (json.isEmpty()) {
    return;
  }
  JsonDocument doc;
  if (deserializeJson(doc, json.c_str())) {
    LOG_ERR("TC", "timecard.json parse failed");
    return;
  }
  JsonArray arr = doc["days"].as<JsonArray>();
  for (JsonObject obj : arr) {
    TimecardDay day;
    day.ymd = obj["d"] | 0;
    if (day.ymd < 19700101) {
      continue;
    }
    day.punches[0] = static_cast<int16_t>(obj["in"] | -1);
    day.punches[1] = static_cast<int16_t>(obj["ls"] | -1);
    day.punches[2] = static_cast<int16_t>(obj["le"] | -1);
    day.punches[3] = static_cast<int16_t>(obj["out"] | -1);
    days.push_back(day);
  }
}

bool TimecardStore::saveToFile() const {
  JsonDocument doc;
  JsonArray arr = doc["days"].to<JsonArray>();
  for (const auto& day : days) {
    if (!day.hasAny()) {
      continue;
    }
    JsonObject obj = arr.add<JsonObject>();
    obj["d"] = day.ymd;
    if (day.punches[0] >= 0) obj["in"] = day.punches[0];
    if (day.punches[1] >= 0) obj["ls"] = day.punches[1];
    if (day.punches[2] >= 0) obj["le"] = day.punches[2];
    if (day.punches[3] >= 0) obj["out"] = day.punches[3];
  }
  String json;
  serializeJson(doc, json);
  Storage.ensureDirectoryExists("/.crosspoint");
  return Storage.writeFile(kPath, json);
}

TimecardDay TimecardStore::getDay(int ymd) const {
  for (const auto& day : days) {
    if (day.ymd == ymd) {
      return day;
    }
  }
  TimecardDay empty;
  empty.ymd = ymd;
  return empty;
}

void TimecardStore::setPunch(int ymd, TimecardPunch punch, int16_t minutesFromMidnight) {
  TimecardDay* found = nullptr;
  for (auto& day : days) {
    if (day.ymd == ymd) {
      found = &day;
      break;
    }
  }
  if (found == nullptr) {
    TimecardDay day;
    day.ymd = ymd;
    days.push_back(day);
    found = &days.back();
  }
  found->set(punch, static_cast<int16_t>(clampMinutes(minutesFromMidnight)));
  if (static_cast<int>(days.size()) > kMaxDays) {
    days.erase(days.begin(), days.begin() + (days.size() - kMaxDays));
  }
  saveToFile();
}

void TimecardStore::clearPunch(int ymd, TimecardPunch punch) {
  setPunch(ymd, punch, -1);
}

namespace TimecardTime {
namespace {
tm localFromYmd(int ymd) {
  int year = 0, month = 0, day = 0;
  splitYmd(ymd, year, month, day);
  tm t = {};
  t.tm_year = year - 1900;
  t.tm_mon = month - 1;
  t.tm_mday = day;
  t.tm_hour = 12;
  t.tm_isdst = -1;
  mktime(&t);
  return t;
}
}  // namespace

int todayYmd() {
  const time_t now = time(nullptr);
  tm t = {};
  localtime_r(&now, &t);
  return (t.tm_year + 1900) * 10000 + (t.tm_mon + 1) * 100 + t.tm_mday;
}

int currentMinutes() {
  const time_t now = time(nullptr);
  tm t = {};
  localtime_r(&now, &t);
  return t.tm_hour * 60 + t.tm_min;
}

void splitYmd(int ymd, int& year, int& month, int& day) {
  year = ymd / 10000;
  month = (ymd / 100) % 100;
  day = ymd % 100;
}

int addDays(int ymd, int days) {
  tm t = localFromYmd(ymd);
  t.tm_mday += days;
  t.tm_isdst = -1;
  mktime(&t);
  return (t.tm_year + 1900) * 10000 + (t.tm_mon + 1) * 100 + t.tm_mday;
}

int sundayYmd(int weekOffset) {
  const time_t now = time(nullptr);
  tm t = {};
  localtime_r(&now, &t);
  t.tm_mday -= t.tm_wday;
  t.tm_mday += weekOffset * 7;
  t.tm_hour = 12;
  t.tm_min = 0;
  t.tm_sec = 0;
  t.tm_isdst = -1;
  mktime(&t);
  return (t.tm_year + 1900) * 10000 + (t.tm_mon + 1) * 100 + t.tm_mday;
}

int weekOfYear(int ymd) {
  const tm t = localFromYmd(ymd);
  return t.tm_yday / 7 + 1;
}

const char* monthAbbrev(int month) {
  static const char* names[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
  if (month < 1 || month > 12) {
    return "?";
  }
  return names[month - 1];
}

const char* weekdayAbbrev(int ymd) {
  static const char* names[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
  const tm t = localFromYmd(ymd);
  if (t.tm_wday < 0 || t.tm_wday > 6) {
    return "Day";
  }
  return names[t.tm_wday];
}

std::string formatAmpm(int16_t minutes) {
  if (minutes < 0) {
    return "--";
  }
  const int hour24 = minutes / 60;
  const int minute = minutes % 60;
  const int hour12 = (hour24 % 12) == 0 ? 12 : (hour24 % 12);
  const char* suffix = hour24 >= 12 ? "PM" : "AM";
  char buf[16];
  snprintf(buf, sizeof(buf), "%d:%02d %s", hour12, minute, suffix);
  return buf;
}

bool parseAmpm(const char* text, int16_t& minutesOut) {
  if (text == nullptr) {
    return false;
  }
  const char* p = text;
  while (*p && isspace(static_cast<unsigned char>(*p))) {
    ++p;
  }
  if (*p == '\0') {
    minutesOut = -1;
    return true;
  }
  int hour = 0;
  int minute = 0;
  if (sscanf(p, "%d:%d", &hour, &minute) != 2) {
    if (sscanf(p, "%d", &hour) != 1) {
      return false;
    }
    minute = 0;
  }
  while (*p && !isalpha(static_cast<unsigned char>(*p))) {
    ++p;
  }
  bool pm = false;
  bool am = false;
  if (*p == 'p' || *p == 'P') {
    pm = true;
  } else if (*p == 'a' || *p == 'A') {
    am = true;
  }
  if (hour < 0 || minute < 0 || minute > 59) {
    return false;
  }
  if (am || pm) {
    if (hour < 1 || hour > 12) {
      return false;
    }
    hour %= 12;
    if (pm) {
      hour += 12;
    }
  } else if (hour > 23) {
    return false;
  }
  minutesOut = static_cast<int16_t>(hour * 60 + minute);
  return true;
}

std::string formatWeekLabel(int sunday) {
  const int saturday = addDays(sunday, 6);
  int y1, m1, d1, y2, m2, d2;
  splitYmd(sunday, y1, m1, d1);
  splitYmd(saturday, y2, m2, d2);
  char buf[48];
  if (m1 == m2) {
    snprintf(buf, sizeof(buf), "Week %d  %s %d – %d", weekOfYear(sunday), monthAbbrev(m1), d1, d2);
  } else {
    snprintf(buf, sizeof(buf), "Week %d  %s %d – %s %d", weekOfYear(sunday), monthAbbrev(m1), d1, monthAbbrev(m2),
             d2);
  }
  return buf;
}

int16_t workedMinutes(const TimecardDay& day) {
  const int16_t in = day.get(TimecardPunch::ClockIn);
  const int16_t out = day.get(TimecardPunch::ClockOut);
  if (in < 0 || out < 0 || out < in) {
    return -1;
  }
  int16_t total = static_cast<int16_t>(out - in);
  const int16_t lunchStart = day.get(TimecardPunch::LunchStart);
  const int16_t lunchEnd = day.get(TimecardPunch::LunchEnd);
  if (lunchStart >= 0 && lunchEnd >= lunchStart) {
    total = static_cast<int16_t>(total - (lunchEnd - lunchStart));
  }
  return total < 0 ? 0 : total;
}
}  // namespace TimecardTime
