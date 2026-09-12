#pragma once

#include <string>

#include "../Activity.h"
#include "TimecardStore.h"
#include "util/ButtonNavigator.h"

class TimecardActivity final : public Activity {
 public:
  enum class Screen { WeekList, Week, Day };

  explicit TimecardActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("Timecard", renderer, mappedInput) {}
  void onEnter() override;
  void loop() override;
  bool onTouchTap(int16_t x, int16_t y) override;
  void render(RenderLock&&) override;

 private:
  static constexpr int kDayCount = 7;
  static constexpr int kPunchCount = 4;
  static constexpr int kWeekHistory = 20;

  ButtonNavigator buttonNavigator;
  Screen screen = Screen::WeekList;
  int weekOffset = 0;
  int selectedIndex = 0;
  int editingYmd = 0;
  int listTop = 0;
  int listRowHeight = 48;
  std::string statusLine;

  int sunday() const { return TimecardTime::sundayYmd(weekOffset); }
  int ymdAt(int dayIndex) const { return TimecardTime::addDays(sunday(), dayIndex); }
  int weekListCount() const { return kWeekHistory; }
  int weekItemCount() const { return kDayCount + kPunchCount; }
  int itemCount() const;

  void punchToday(TimecardPunch punch);
  void openWeek(int offset);
  void openDay(int ymd);
  void editSelectedPunch();
  void applyEditedTime(TimecardPunch punch, const std::string& text);
  void moveSelection(int delta);
  void activate();
  void layoutList(int pageHeight, int pageWidth);
};
