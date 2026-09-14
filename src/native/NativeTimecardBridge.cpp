#include "NativeTimecardBridge.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <utility>

#include "MappedInputManager.h"
#include "NativeAppHost.h"
#include "TimecardStore.h"
#include "activities/Activity.h"
#include "activities/ActivityManager.h"
#include "activities/RenderLock.h"
#include "activities/util/KeyboardEntryActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {
constexpr int kDayCount = 7;
constexpr int kPunchCount = 4;
constexpr int kWeekHistory = 20;

struct PendingEdit {
  bool requested = false;
  int ymd = 0;
  uint8_t punch = 0;
  int weekOffset = 0;
};

struct BridgeState {
  GfxRenderer* renderer = nullptr;
  MappedInputManager* input = nullptr;
  PendingEdit edit;
  bool resumeAvailable = false;
  t5_app_timecard_resume_t resume{};
};

BridgeState state;

bool validScreen(uint8_t screen) {
  return screen == T5_APP_TIMECARD_WEEK_LIST || screen == T5_APP_TIMECARD_WEEK || screen == T5_APP_TIMECARD_DAY;
}

bool validPunch(uint8_t punch) { return punch < kPunchCount; }

void copyText(char* dst, size_t capacity, const char* src) {
  if (!dst || capacity == 0) return;
  if (!src) src = "";
  std::strncpy(dst, src, capacity - 1);
  dst[capacity - 1] = '\0';
}

const char* punchLabel(uint8_t punch) {
  switch (punch) {
    case T5_APP_TIMECARD_CLOCK_IN:
      return tr(STR_TIMECARD_CLOCK_IN);
    case T5_APP_TIMECARD_LUNCH_START:
      return tr(STR_TIMECARD_LUNCH_START);
    case T5_APP_TIMECARD_LUNCH_END:
      return tr(STR_TIMECARD_LUNCH_END);
    case T5_APP_TIMECARD_CLOCK_OUT:
    default:
      return tr(STR_TIMECARD_CLOCK_OUT);
  }
}

const char* columnTitle(int column) {
  switch (column) {
    case 0: return "Day";
    case 1: return "In";
    case 2: return "Start";
    case 3: return "End";
    case 4: return "Out";
    default: return "";
  }
}

int itemCount(uint8_t screen) {
  switch (screen) {
    case T5_APP_TIMECARD_WEEK_LIST: return kWeekHistory;
    case T5_APP_TIMECARD_DAY: return kPunchCount;
    case T5_APP_TIMECARD_WEEK:
    default: return kDayCount + kPunchCount;
  }
}

void layoutList(uint8_t screen, int pageHeight, int& listTop, int& listRowHeight) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  listTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing + 8;
  const int bottom = pageHeight - metrics.buttonHintsHeight - 36;
  const int available = std::max(200, bottom - listTop);
  if (screen == T5_APP_TIMECARD_WEEK) {
    listRowHeight = std::max(44, available / (kDayCount + kPunchCount + 2));
  } else {
    listRowHeight = std::max(48, available / std::min(itemCount(screen), 12));
  }
}

void drawSelectedRow(GfxRenderer& renderer, int x, int y, int width, int height, bool selected) {
  if (selected) renderer.fillRect(x, y, width, height, true);
}

void prepareResume(int ymd, uint8_t punch, int weekOffset, const char* status) {
  state.resume = {};
  state.resume.screen = T5_APP_TIMECARD_DAY;
  state.resume.week_offset = weekOffset;
  state.resume.selected_index = punch;
  state.resume.editing_ymd = ymd;
  copyText(state.resume.status, sizeof(state.resume.status), status);
  state.resumeAvailable = true;
}

class NativeTimecardEditActivity final : public Activity {
  int ymd;
  uint8_t punch;
  int weekOffset;
  std::string resumePath;
  bool started = false;
  bool childCompleted = false;
  bool resumeReturned = false;

 public:
  NativeTimecardEditActivity(GfxRenderer& renderer, MappedInputManager& input, int editYmd, uint8_t editPunch,
                             int editWeekOffset, std::string resume)
      : Activity("NativeTimecardEdit", renderer, input),
        ymd(editYmd),
        punch(editPunch),
        weekOffset(editWeekOffset),
        resumePath(std::move(resume)) {}

  void onEnter() override {
    Activity::onEnter();
    if (started) return;
    started = true;

    TIMECARD.loadFromFile();
    const TimecardDay day = TIMECARD.getDay(ymd);
    const int16_t current = day.get(static_cast<TimecardPunch>(punch));
    const std::string initial = current >= 0 ? TimecardTime::formatAmpm(current) : "";

    startActivityForResult(
        std::make_unique<KeyboardEntryActivity>(renderer, mappedInput, punchLabel(punch), initial, 12, InputType::Text),
        [this](const ActivityResult& result) {
          if (result.isCancelled) {
            const TimecardDay existing = TIMECARD.getDay(ymd);
            prepareResume(ymd, punch, weekOffset,
                          existing.hasAny() ? "Confirm a row to edit" : "Confirm a row to set time");
            childCompleted = true;
            return;
          }

          const auto& keyboard = std::get<KeyboardResult>(result.data);
          int16_t minutes = -1;
          if (!TimecardTime::parseAmpm(keyboard.text.c_str(), minutes)) {
            prepareResume(ymd, punch, weekOffset, tr(STR_TIMECARD_EDIT_TIME));
            childCompleted = true;
            return;
          }

          TIMECARD.setPunch(ymd, static_cast<TimecardPunch>(punch), minutes);
          const std::string status = std::string(punchLabel(punch)) + "  " + TimecardTime::formatAmpm(minutes);
          prepareResume(ymd, punch, weekOffset, status.c_str());
          childCompleted = true;
        });
  }

  void loop() override {
    if (resumeReturned) {
      finish();
      return;
    }
    if (!childCompleted) return;
    childCompleted = false;
    if (resumePath.empty()) {
      finish();
      return;
    }

    const esp_err_t result = runNativeApp(resumePath.c_str(), renderer, mappedInput);
    if (result != ESP_OK) {
      finish();
      return;
    }

    // If the resumed ELF requests another edit, runNativeApp queues another
    // wrapper before returning here. Let ActivityManager process that push first.
    resumeReturned = true;
  }

  void render(RenderLock&&) override {
    renderer.clearScreen();
    const auto& metrics = UITheme::getInstance().getMetrics();
    GUI.drawHeader(renderer, Rect{0, metrics.topPadding, renderer.getScreenWidth(), metrics.headerHeight},
                   tr(STR_TIMECARD));
    renderer.displayBuffer(HalDisplay::BALANCED_REFRESH);
  }
};
}  // namespace

void nativeTimecardBegin(GfxRenderer& renderer, MappedInputManager& input) {
  state.renderer = &renderer;
  state.input = &input;
  TIMECARD.loadFromFile();
}

void nativeTimecardEnd() {
  state.renderer = nullptr;
  state.input = nullptr;
}

void nativeTimecardReload() { TIMECARD.loadFromFile(); }

int32_t nativeTimecardTodayYmd() { return TimecardTime::todayYmd(); }

int32_t nativeTimecardCurrentMinutes() { return TimecardTime::currentMinutes(); }

int32_t nativeTimecardSundayYmd(int32_t weekOffset) { return TimecardTime::sundayYmd(weekOffset); }

int32_t nativeTimecardAddDays(int32_t ymd, int32_t days) { return TimecardTime::addDays(ymd, days); }

bool nativeTimecardGetDay(int32_t ymd, t5_app_timecard_day_t* out) {
  if (!out || ymd < 19700101) return false;
  const TimecardDay day = TIMECARD.getDay(ymd);
  *out = {};
  out->ymd = day.ymd;
  for (int i = 0; i < kPunchCount; ++i) out->punches[i] = day.punches[i];
  return true;
}

bool nativeTimecardSetPunch(int32_t ymd, uint8_t punch, int16_t minutesFromMidnight) {
  if (ymd < 19700101 || !validPunch(punch)) return false;
  TIMECARD.setPunch(ymd, static_cast<TimecardPunch>(punch), minutesFromMidnight);
  return true;
}

bool nativeTimecardPunchLabel(uint8_t punch, char* label, size_t capacity) {
  if (!validPunch(punch) || !label || capacity == 0) return false;
  copyText(label, capacity, punchLabel(punch));
  return true;
}

bool nativeTimecardFormatAmpm(int16_t minutesFromMidnight, char* text, size_t capacity) {
  if (!text || capacity == 0) return false;
  const std::string value = TimecardTime::formatAmpm(minutesFromMidnight);
  copyText(text, capacity, value.c_str());
  return true;
}

void nativeTimecardRender(uint8_t screen, int32_t weekOffset, int32_t selectedIndex, int32_t editingYmd,
                          const char* status) {
  if (!state.renderer || !state.input || !validScreen(screen)) return;

  auto& renderer = *state.renderer;
  auto& input = *state.input;
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();
  renderer.clearScreen();

  int listTop = 0;
  int listRowHeight = 48;
  layoutList(screen, pageHeight, listTop, listRowHeight);

  const int left = metrics.contentSidePadding;
  const int rowWidth = pageWidth - left * 2;

  if (screen == T5_APP_TIMECARD_WEEK_LIST) {
    GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_TIMECARD), "Weeks");
    GUI.drawList(
        renderer, Rect{0, listTop, pageWidth, pageHeight - listTop - metrics.buttonHintsHeight - 8}, kWeekHistory,
        selectedIndex,
        [](int index) { return TimecardTime::formatWeekLabel(TimecardTime::sundayYmd(-index)); },
        [](int index) { return index == 0 ? "This week" : ""; });
  } else if (screen == T5_APP_TIMECARD_DAY) {
    int year = 0, month = 0, day = 0;
    TimecardTime::splitYmd(editingYmd, year, month, day);
    char subtitle[40];
    snprintf(subtitle, sizeof(subtitle), "%s %s %d", TimecardTime::weekdayAbbrev(editingYmd),
             TimecardTime::monthAbbrev(month), day);
    GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_TIMECARD), subtitle);
    const TimecardDay punches = TIMECARD.getDay(editingYmd);
    GUI.drawList(
        renderer, Rect{0, listTop, pageWidth, pageHeight - listTop - metrics.buttonHintsHeight - 8}, kPunchCount,
        selectedIndex, [](int index) { return std::string(punchLabel(static_cast<uint8_t>(index))); }, nullptr, nullptr,
        [&punches](int index) {
          return TimecardTime::formatAmpm(punches.get(static_cast<TimecardPunch>(index)));
        },
        true);
  } else {
    const int sunday = TimecardTime::sundayYmd(weekOffset);
    const std::string weekTitle = TimecardTime::formatWeekLabel(sunday);
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
      const int ymd = TimecardTime::addDays(sunday, i);
      const TimecardDay dayData = TIMECARD.getDay(ymd);
      int year = 0, month = 0, dom = 0;
      TimecardTime::splitYmd(ymd, year, month, dom);
      char dayLabel[16];
      snprintf(dayLabel, sizeof(dayLabel), "%s %d%s", TimecardTime::weekdayAbbrev(ymd), dom, ymd == today ? "*" : "");
      const bool selected = selectedIndex == i;
      drawSelectedRow(renderer, left - 4, y, rowWidth + 8, listRowHeight - 2, selected);
      const bool ink = !selected;
      renderer.drawText(UI_10_FONT_ID, colX[0], y + 12, dayLabel, ink);
      renderer.drawText(UI_10_FONT_ID, colX[1], y + 12,
                        TimecardTime::formatAmpm(dayData.get(TimecardPunch::ClockIn)).c_str(), ink);
      renderer.drawText(UI_10_FONT_ID, colX[2], y + 12,
                        TimecardTime::formatAmpm(dayData.get(TimecardPunch::LunchStart)).c_str(), ink);
      renderer.drawText(UI_10_FONT_ID, colX[3], y + 12,
                        TimecardTime::formatAmpm(dayData.get(TimecardPunch::LunchEnd)).c_str(), ink);
      renderer.drawText(UI_10_FONT_ID, colX[4], y + 12,
                        TimecardTime::formatAmpm(dayData.get(TimecardPunch::ClockOut)).c_str(), ink);
    }

    for (int i = 0; i < kPunchCount; ++i) {
      const int y = tableTop + (kDayCount + i) * listRowHeight;
      const bool selected = selectedIndex == kDayCount + i;
      drawSelectedRow(renderer, left - 4, y, rowWidth + 8, listRowHeight - 2, selected);
      renderer.drawText(UI_12_FONT_ID, left, y + 12, punchLabel(static_cast<uint8_t>(i)), !selected);
    }
  }

  const int statusY = pageHeight - metrics.buttonHintsHeight - 22;
  const std::string statusText = renderer.truncatedText(SMALL_FONT_ID, status ? status : "", rowWidth);
  renderer.drawText(SMALL_FONT_ID, left, statusY, statusText.c_str());

  const char* confirm = screen == T5_APP_TIMECARD_WEEK_LIST ? tr(STR_OPEN) : tr(STR_SELECT);
  const auto labels = input.mapLabels(screen == T5_APP_TIMECARD_WEEK_LIST ? tr(STR_HOME) : tr(STR_BACK), confirm,
                                      tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}

uint8_t nativeTimecardTouch(uint8_t screen, int16_t, int16_t y, int32_t* selectedIndex) {
  if (!state.renderer || !validScreen(screen) || !selectedIndex) return T5_APP_TIMECARD_TOUCH_NONE;

  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageHeight = state.renderer->getScreenHeight();
  int listTop = 0;
  int listRowHeight = 48;
  layoutList(screen, pageHeight, listTop, listRowHeight);

  if (y < metrics.topPadding + metrics.headerHeight) {
    return screen == T5_APP_TIMECARD_WEEK_LIST ? T5_APP_TIMECARD_TOUCH_NONE : T5_APP_TIMECARD_TOUCH_HEADER;
  }

  int tableTop = listTop;
  if (screen == T5_APP_TIMECARD_WEEK) tableTop = listTop + listRowHeight;
  if (y < tableTop) {
    return screen == T5_APP_TIMECARD_WEEK ? T5_APP_TIMECARD_TOUCH_HEADER : T5_APP_TIMECARD_TOUCH_NONE;
  }

  const int index = (y - tableTop) / listRowHeight;
  if (index < 0 || index >= itemCount(screen)) return T5_APP_TIMECARD_TOUCH_NONE;
  *selectedIndex = index;
  return T5_APP_TIMECARD_TOUCH_ITEM;
}

bool nativeTimecardRequestEdit(int32_t ymd, uint8_t punch, int32_t weekOffset) {
  if (!state.renderer || !state.input || ymd < 19700101 || !validPunch(punch)) return false;
  state.edit.requested = true;
  state.edit.ymd = ymd;
  state.edit.punch = punch;
  state.edit.weekOffset = weekOffset;
  state.resumeAvailable = false;
  return true;
}

bool nativeTimecardTakeResume(t5_app_timecard_resume_t* out) {
  if (!out || !state.resumeAvailable) return false;
  *out = state.resume;
  state.resumeAvailable = false;
  return true;
}

void nativeTimecardDispatchPendingAction(GfxRenderer& renderer, MappedInputManager& input, const char* resumePath) {
  if (!state.edit.requested) return;
  const PendingEdit edit = state.edit;
  state.edit = {};
  activityManager.pushActivity(std::make_unique<NativeTimecardEditActivity>(
      renderer, input, edit.ymd, edit.punch, edit.weekOffset, resumePath ? resumePath : ""));
}
