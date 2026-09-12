#pragma once

#include <string>

#include "../Activity.h"
#include "TimecardStore.h"
#include "util/ButtonNavigator.h"

class TimecardActivity final : public Activity {
  ButtonNavigator buttonNavigator;
  int weekOffset = 0;
  int selectedIndex = 0;
  int editingYmd = 0;
  std::string statusLine;

  static constexpr int kDayCount = 7;
  static constexpr int kPunchCount = 4;
  static constexpr int kWeekItemCount = kDayCount + kPunchCount;

  bool isEditing() const { return editingYmd != 0; }
  int itemCount() const { return isEditing() ? kPunchCount : kWeekItemCount; }
  int sunday() const { return TimecardTime::sundayYmd(weekOffset); }
  int ymdAt(int dayIndex) const { return TimecardTime::addDays(sunday(), dayIndex); }

  void punchToday(TimecardPunch punch);
  void openDay(int ymd);
  void editSelectedPunch();
  void applyEditedTime(TimecardPunch punch, const std::string& text);

 public:
  explicit TimecardActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("Timecard", renderer, mappedInput) {}
  void onEnter() override;
  void loop() override;
  bool onTouchTap(int16_t x, int16_t y) override;
  void render(RenderLock&&) override;
};
