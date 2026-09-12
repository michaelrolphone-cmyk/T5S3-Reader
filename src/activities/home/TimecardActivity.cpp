#include "TimecardActivity.h"

#include <I18n.h>
#include <algorithm>
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

const char* columnTitle(int column) {
  switch (column) {
    case 0:
      return "Day";
    case 1:
      return "In";
    case 2:
      return "Start";
    case 3:
      return "End";
    case 4:
      return "Out";
    default:
      return "";
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

void drawSelectedRow(GfxRenderer& renderer, int x, int y, int width, int height, bool selected) {
  if (selected) {
    renderer.fillRect(x, y, width, height, true);
  }
}
}  // namespace

int TimecardActivity::itemCount() const {
  switch (screen) {
    case Screen::WeekList:
      return weekListCount();
    case Screen::Day:
      return kPunchCount;
    case Screen::Week:
    default:
      return weekItemCount();
  }
}

void TimecardActivity::layoutList(int pageHeight, int /*pageWidth*/) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  listTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing + 8;
  const int bottom = pageHeight - metrics.buttonHintsHeight - 36;
  const int available = std::max(200, bottom - listTop);
  if (screen == Screen::Week) {
    listRowHeight = std::max(44, available / (kDayCount + kPunchCount + 2));
  } else {
    listRowHeight = std::max(48, available / std::min(itemCount(), 12));
  }
}

void TimecardActivity::onEnter() {
  Activity::onEnter();
  TIMECARD.loadFromFile();
  if (screen == Screen::WeekList && weekOffset == 0 && selectedIndex == 0 && editingYmd == 0) {
    statusLine = "Select a week";
  }
  requestUpdate();
}

void TimecardActivity::punchToday(TimecardPunch punch) {
  const int today = TimecardTime::todayYmd();
  const int minutes = TimecardTime::currentMinutes();
  TIMECARD.setPunch(today, punch, static_cast<int16_t>(minutes));
  weekOffset = 0;
  screen = Screen::Week;
  selectedIndex = today == ymdAt(6) ? 6 : std::max(0, TimecardTime::sundayYmd(0) == sunday() ? 0 : 0);
  {
    const int sun = TimecardTime::sundayYmd(0);
    for (int i = 0; i < kDayCount; ++i) {
      if (TimecardTime::addDays(sun, i) == today) {
        selectedIndex = i;
        break;
      }
    }
  }
  statusLine = std::string(punchLabel(punch)) + "  " + TimecardTime::formatAmpm(static_cast<int16_t>(minutes));
  requestUpdate();
}

void TimecardActivity::openWeek(int offset) {
  weekOffset = offset;
  screen = Screen::Week;
  selectedIndex = 0;
  if (weekOffset == 0) {
    const int today = TimecardTime::todayYmd();
    for (int i = 0; i < kDayCount; ++i) {
      if (ymdAt(i) == today) {
        selectedIndex = i;
        break;
      }
    }
  }
  statusLine = "Open a day, or punch below";
  requestUpdate();
}

void TimecardActivity::openDay(int ymd) {
  TIMECARD.loadFromFile();
  editingYmd = ymd;
  screen = Screen::Day;
  selectedIndex = 0;
  const TimecardDay day = TIMECARD.getDay(ymd);
  statusLine = day.hasAny() ? "Confirm a row to edit" : "Confirm a row to set time";
  requestUpdate();
}

void TimecardActivity::applyEditedTime(TimecardPunch punch, const std::string& text) {
  int16_t minutes = -1;
  if (!TimecardTime::parseAmpm(text.c_str(), minutes)) {
    statusLine = tr(STR_TIMECARD_EDIT_TIME);
    requestUpdate();
    return;
  }
  TIMECARD.setPunch(editingYmd, punch, minutes);
  statusLine = std::string(punchLabel(punch)) + "  " + TimecardTime::formatAmpm(minutes);
  requestUpdate();
}

void TimecardActivity::editSelectedPunch() {
  if (screen != Screen::Day) {
    return;
  }
  const auto punch = static_cast<TimecardPunch>(selectedIndex);
  const TimecardDay day = TIMECARD.getDay(editingYmd);
  const int16_t current = day.get(punch);
  const std::string initial = current >= 0 ? TimecardTime::formatAmpm(current) : "";
  startActivityForResult(
      std::make_unique<KeyboardEntryActivity>(renderer, mappedInput, punchLabel(punch), initial, 12, InputType::Text),
      [this, punch](const ActivityResult& result) {
        if (result.isCancelled) {
          return;
        }
        const auto& keyboard = std::get<KeyboardResult>(result.data);
        applyEditedTime(punch, keyboard.text);
      });
}

void TimecardActivity::moveSelection(int delta) {
  const int count = itemCount();
  if (count <= 0) {
    return;
  }
  selectedIndex = (selectedIndex + delta + count) % count;
  requestUpdate();
}

void TimecardActivity::activate() {
  if (screen == Screen::WeekList) {
    openWeek(-selectedIndex);
    return;
  }
  if (screen == Screen::Day) {
    editSelectedPunch();
    return;
  }
  if (selectedIndex < kDayCount) {
    openDay(ymdAt(selectedIndex));
  } else {
    punchToday(static_cast<TimecardPunch>(selectedIndex - kDayCount));
  }
}

void TimecardActivity::loop() {
  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    if (screen == Screen::Day) {
      screen = Screen::Week;
      editingYmd = 0;
      selectedIndex = 0;
      statusLine = "Open a day, or punch below";
      requestUpdate();
      return;
    }
    if (screen == Screen::Week) {
      screen = Screen::WeekList;
      selectedIndex = -weekOffset;
      if (selectedIndex < 0) {
        selectedIndex = 0;
      }
      if (selectedIndex >= weekListCount()) {
        selectedIndex = weekListCount() - 1;
      }
      statusLine = "Select a week";
      requestUpdate();
      return;
    }
    activityManager.goHome();
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    activate();
    return;
  }

  buttonNavigator.onNext([this] { moveSelection(1); });
  buttonNavigator.onPrevious([this] { moveSelection(-1); });
}

bool TimecardActivity::onTouchTap(int16_t, int16_t y) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageHeight = renderer.getScreenHeight();
  layoutList(pageHeight, renderer.getScreenWidth());

  if (y < metrics.topPadding + metrics.headerHeight) {
    if (screen != Screen::WeekList) {
      screen = Screen::WeekList;
      selectedIndex = -weekOffset;
      requestUpdate();
      return true;
    }
    return false;
  }

  int tableTop = listTop;
  if (screen == Screen::Week) {
    tableTop = listTop + listRowHeight;
  }
  if (y < tableTop) {
    if (screen == Screen::Week) {
      screen = Screen::WeekList;
      selectedIndex = -weekOffset;
      requestUpdate();
      return true;
    }
    return false;
  }

  const int index = (y - tableTop) / listRowHeight;
  if (index < 0 || index >= itemCount()) {
    return false;
  }
  selectedIndex = index;
  activate();
  return true;
}

void TimecardActivity::render(RenderLock&&) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();
  renderer.clearScreen();
  layoutList(pageHeight, pageWidth);

  const int left = metrics.contentSidePadding;
  const int rowWidth = pageWidth - left * 2;

  if (screen == Screen::WeekList) {
    GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_TIMECARD),
                   "Weeks");
    GUI.drawList(
        renderer, Rect{0, listTop, pageWidth, pageHeight - listTop - metrics.buttonHintsHeight - 8},
        weekListCount(), selectedIndex,
        [this](int index) {
          const int sun = TimecardTime::sundayYmd(-index);
          return TimecardTime::formatWeekLabel(sun);
        },
        [this](int index) { return index == 0 ? "This week" : ""; });
  } else if (screen == Screen::Day) {
    int year = 0, month = 0, day = 0;
    TimecardTime::splitYmd(editingYmd, year, month, day);
    char subtitle[40];
    snprintf(subtitle, sizeof(subtitle), "%s %s %d", TimecardTime::weekdayAbbrev(editingYmd),
             TimecardTime::monthAbbrev(month), day);
    GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_TIMECARD), subtitle);
    const TimecardDay punches = TIMECARD.getDay(editingYmd);
    GUI.drawList(
        renderer, Rect{0, listTop, pageWidth, pageHeight - listTop - metrics.buttonHintsHeight - 8}, kPunchCount,
        selectedIndex, [](int index) { return std::string(punchLabel(static_cast<TimecardPunch>(index))); }, nullptr,
        nullptr,
        [&punches](int index) {
          return TimecardTime::formatAmpm(punches.get(static_cast<TimecardPunch>(index)));
        },
        true);
  } else {
    const std::string weekTitle = TimecardTime::formatWeekLabel(sunday());
    GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_TIMECARD),
                   weekTitle.c_str());

    const int colX[5] = {left, left + 72, left + 162, left + 262, left + 362};
    const int headerY = listTop;
    renderer.drawText(UI_10_FONT_ID, colX[0], headerY + 8, columnTitle(0));
    renderer.drawText(UI_10_FONT_ID, colX[1], headerY + 8, columnTitle(1));
    renderer.drawText(UI_10_FONT_ID, colX[2], headerY + 8, columnTitle(2));
    renderer.drawText(UI_10_FONT_ID, colX[3], headerY + 8, columnTitle(3));
    renderer.drawText(UI_10_FONT_ID, colX[4], headerY + 8, columnTitle(4));
    renderer.drawLine(left, headerY + listRowHeight - 6, left + rowWidth, headerY + listRowHeight - 6, true);

    const int tableTop = listTop + listRowHeight;
    const int today = TimecardTime::todayYmd();
    for (int i = 0; i < kDayCount; ++i) {
      const int y = tableTop + i * listRowHeight;
      const int ymd = ymdAt(i);
      const TimecardDay day = TIMECARD.getDay(ymd);
      int year = 0, month = 0, dom = 0;
      TimecardTime::splitYmd(ymd, year, month, dom);
      char dayLabel[16];
      snprintf(dayLabel, sizeof(dayLabel), "%s %d%s", TimecardTime::weekdayAbbrev(ymd), dom, ymd == today ? "*" : "");
      const bool selected = selectedIndex == i;
      drawSelectedRow(renderer, left - 4, y, rowWidth + 8, listRowHeight - 2, selected);
      const bool ink = !selected;
      renderer.drawText(UI_10_FONT_ID, colX[0], y + 12, dayLabel, ink);
      renderer.drawText(UI_10_FONT_ID, colX[1], y + 12,
                        TimecardTime::formatAmpm(day.get(TimecardPunch::ClockIn)).c_str(), ink);
      renderer.drawText(UI_10_FONT_ID, colX[2], y + 12,
                        TimecardTime::formatAmpm(day.get(TimecardPunch::LunchStart)).c_str(), ink);
      renderer.drawText(UI_10_FONT_ID, colX[3], y + 12,
                        TimecardTime::formatAmpm(day.get(TimecardPunch::LunchEnd)).c_str(), ink);
      renderer.drawText(UI_10_FONT_ID, colX[4], y + 12,
                        TimecardTime::formatAmpm(day.get(TimecardPunch::ClockOut)).c_str(), ink);
    }

    for (int i = 0; i < kPunchCount; ++i) {
      const int y = tableTop + (kDayCount + i) * listRowHeight;
      const bool selected = selectedIndex == kDayCount + i;
      drawSelectedRow(renderer, left - 4, y, rowWidth + 8, listRowHeight - 2, selected);
      renderer.drawText(UI_12_FONT_ID, left, y + 12, punchLabel(static_cast<TimecardPunch>(i)), !selected);
    }
  }

  const int statusY = pageHeight - metrics.buttonHintsHeight - 22;
  const std::string status = renderer.truncatedText(SMALL_FONT_ID, statusLine.c_str(), rowWidth);
  renderer.drawText(SMALL_FONT_ID, left, statusY, status.c_str());

  const char* confirm =
      screen == Screen::WeekList ? tr(STR_OPEN) : (screen == Screen::Day ? tr(STR_SELECT) : tr(STR_SELECT));
  const auto labels = mappedInput.mapLabels(screen == Screen::WeekList ? tr(STR_HOME) : tr(STR_BACK), confirm,
                                            tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}
