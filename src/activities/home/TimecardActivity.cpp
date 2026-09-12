#include "TimecardActivity.h"

#include <I18n.h>
#include <cstdio>

#include "MappedInputManager.h"
#include "activities/util/KeyboardEntryActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {
const char* punchLabel(TimecardPunch punch) {
  switch (punch) {
    case TimecardPunch::ClockIn:
      return tr(STR_TIMECARD_CLOCK_IN);
    case TimecardPunch::LunchStart:
      return tr(STR_TIMECARD_LUNCH_START);
    case TimecardPunch::LunchEnd:
      return tr(STR_TIMECARD_LUNCH_END);
    case TimecardPunch::ClockOut:
    default:
      return tr(STR_TIMECARD_CLOCK_OUT);
  }
}

std::string hoursLabel(int16_t minutes) {
  if (minutes < 0) {
    return "";
  }
  char buf[16];
  snprintf(buf, sizeof(buf), "%d:%02d", minutes / 60, minutes % 60);
  return buf;
}
}  // namespace

void TimecardActivity::onEnter() {
  Activity::onEnter();
  TIMECARD.loadFromFile();
  weekOffset = 0;
  selectedIndex = 0;
  editingYmd = 0;
  statusLine = tr(STR_TIMECARD_HINT);
  requestUpdate();
}

void TimecardActivity::punchToday(TimecardPunch punch) {
  const int today = TimecardTime::todayYmd();
  const int minutes = TimecardTime::currentMinutes();
  TIMECARD.setPunch(today, punch, static_cast<int16_t>(minutes));
  statusLine = std::string(punchLabel(punch)) + "  " + TimecardTime::formatAmpm(static_cast<int16_t>(minutes));
  weekOffset = 0;
  requestUpdate();
}

void TimecardActivity::openDay(int ymd) {
  editingYmd = ymd;
  selectedIndex = 0;
  statusLine = tr(STR_TIMECARD_EDIT_TIME);
  requestUpdate();
}

void TimecardActivity::applyEditedTime(TimecardPunch punch, const std::string& text) {
  int16_t minutes = -1;
  if (!TimecardTime::parseAmpm(text.c_str(), minutes)) {
    statusLine = tr(STR_TIMECARD_HINT);
    requestUpdate();
    return;
  }
  TIMECARD.setPunch(editingYmd, punch, minutes);
  statusLine = std::string(punchLabel(punch)) + "  " + TimecardTime::formatAmpm(minutes);
  requestUpdate();
}

void TimecardActivity::editSelectedPunch() {
  if (!isEditing()) {
    return;
  }
  const auto punch = static_cast<TimecardPunch>(selectedIndex);
  const TimecardDay day = TIMECARD.getDay(editingYmd);
  const int16_t current = day.get(punch);
  const std::string initial = current >= 0 ? TimecardTime::formatAmpm(current) : "";
  startActivityForResult(
      std::make_unique<KeyboardEntryActivity>(renderer, mappedInput, punchLabel(punch), initial, 10, InputType::Text),
      [this, punch](const ActivityResult& result) {
        if (result.isCancelled) {
          return;
        }
        const auto& keyboard = std::get<KeyboardResult>(result.data);
        applyEditedTime(punch, keyboard.text);
      });
}

void TimecardActivity::loop() {
  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    if (isEditing()) {
      editingYmd = 0;
      selectedIndex = 0;
      statusLine = tr(STR_TIMECARD_HINT);
      requestUpdate();
      return;
    }
    activityManager.goHome();
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    if (isEditing()) {
      editSelectedPunch();
      return;
    }
    if (selectedIndex < kDayCount) {
      openDay(ymdAt(selectedIndex));
    } else {
      punchToday(static_cast<TimecardPunch>(selectedIndex - kDayCount));
    }
    return;
  }

  buttonNavigator.onNext([this] {
    if (selectedIndex + 1 < itemCount()) {
      ++selectedIndex;
    } else if (!isEditing() && weekOffset < 0) {
      ++weekOffset;
      selectedIndex = 0;
    }
    requestUpdate();
  });
  buttonNavigator.onPrevious([this] {
    if (selectedIndex > 0) {
      --selectedIndex;
    } else if (!isEditing()) {
      --weekOffset;
      selectedIndex = 0;
    }
    requestUpdate();
  });
}

bool TimecardActivity::onTouchTap(int16_t, int16_t y) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageHeight = renderer.getScreenHeight();
  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing + 28;
  const int rowHeight = metrics.listRowHeight + 4;
  if (y < contentTop) {
    if (!isEditing()) {
      if (y < metrics.topPadding + metrics.headerHeight) {
        --weekOffset;
        requestUpdate();
        return true;
      }
    }
    return false;
  }
  const int index = (y - contentTop) / rowHeight;
  if (index < 0 || index >= itemCount()) {
    if (y > pageHeight - metrics.buttonHintsHeight - 8 && !isEditing() && selectedIndex >= kDayCount) {
      punchToday(static_cast<TimecardPunch>(selectedIndex - kDayCount));
      return true;
    }
    return false;
  }
  selectedIndex = index;
  if (isEditing()) {
    editSelectedPunch();
  } else if (selectedIndex < kDayCount) {
    openDay(ymdAt(selectedIndex));
  } else {
    punchToday(static_cast<TimecardPunch>(selectedIndex - kDayCount));
  }
  return true;
}

void TimecardActivity::render(RenderLock&&) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();
  renderer.clearScreen();

  const int sun = sunday();
  const std::string weekTitle = TimecardTime::formatWeekLabel(sun);
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight},
                 isEditing() ? tr(STR_TIMECARD) : tr(STR_TIMECARD), weekTitle.c_str());

  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentBottom = pageHeight - metrics.buttonHintsHeight - metrics.verticalSpacing;
  const int rowHeight = metrics.listRowHeight + 4;
  const int left = metrics.contentSidePadding;

  if (isEditing()) {
    int y1, m1, d1;
    TimecardTime::splitYmd(editingYmd, y1, m1, d1);
    char dayTitle[32];
    snprintf(dayTitle, sizeof(dayTitle), "%s %s %d", TimecardTime::weekdayAbbrev(editingYmd),
             TimecardTime::monthAbbrev(m1), d1);
    renderer.drawText(UI_12_FONT_ID, left, contentTop, dayTitle);
    const TimecardDay day = TIMECARD.getDay(editingYmd);
    for (int i = 0; i < kPunchCount; ++i) {
      const int y = contentTop + 28 + i * rowHeight;
      const bool selected = selectedIndex == i;
      if (selected) {
        renderer.fillRect(left - 6, y - 4, pageWidth - left, rowHeight - 2, false);
      }
      const auto punch = static_cast<TimecardPunch>(i);
      const std::string line =
          std::string(punchLabel(punch)) + "   " + TimecardTime::formatAmpm(day.get(punch));
      renderer.drawText(UI_12_FONT_ID, left, y, line.c_str(), !selected);
    }
  } else {
    renderer.drawText(SMALL_FONT_ID, left, contentTop, "In     Lunch        Out");
    const int today = TimecardTime::todayYmd();
    for (int i = 0; i < kDayCount; ++i) {
      const int ymd = ymdAt(i);
      const TimecardDay day = TIMECARD.getDay(ymd);
      int year = 0, month = 0, dom = 0;
      TimecardTime::splitYmd(ymd, year, month, dom);
      char leftCol[24];
      snprintf(leftCol, sizeof(leftCol), "%s %d", TimecardTime::weekdayAbbrev(ymd), dom);
      char times[48];
      snprintf(times, sizeof(times), "%-8s %-8s %-8s %-8s",
               TimecardTime::formatAmpm(day.get(TimecardPunch::ClockIn)).c_str(),
               TimecardTime::formatAmpm(day.get(TimecardPunch::LunchStart)).c_str(),
               TimecardTime::formatAmpm(day.get(TimecardPunch::LunchEnd)).c_str(),
               TimecardTime::formatAmpm(day.get(TimecardPunch::ClockOut)).c_str());
      const int y = contentTop + 18 + i * rowHeight;
      const bool selected = selectedIndex == i;
      if (selected) {
        renderer.fillRect(left - 6, y - 4, pageWidth - left, rowHeight - 2, false);
      }
      std::string line = leftCol;
      line += (ymd == today) ? "* " : "  ";
      line += times;
      const int16_t worked = TimecardTime::workedMinutes(day);
      if (worked >= 0) {
        line += "  ";
        line += hoursLabel(worked);
      }
      renderer.drawText(SMALL_FONT_ID, left, y, line.c_str(), !selected);
    }

    for (int i = 0; i < kPunchCount; ++i) {
      const int y = contentTop + 18 + (kDayCount + i) * rowHeight;
      const bool selected = selectedIndex == kDayCount + i;
      if (selected) {
        renderer.fillRect(left - 6, y - 4, pageWidth - left, rowHeight - 2, false);
      }
      renderer.drawText(UI_12_FONT_ID, left, y, punchLabel(static_cast<TimecardPunch>(i)), !selected);
    }
  }

  const std::string status = renderer.truncatedText(SMALL_FONT_ID, statusLine.c_str(), pageWidth - left * 2);
  renderer.drawText(SMALL_FONT_ID, left, contentBottom - 18, status.c_str());

  const auto labels =
      mappedInput.mapLabels(isEditing() ? tr(STR_BACK) : tr(STR_HOME),
                            isEditing() ? tr(STR_SELECT) : (selectedIndex < kDayCount ? tr(STR_OPEN) : tr(STR_SELECT)),
                            tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}
