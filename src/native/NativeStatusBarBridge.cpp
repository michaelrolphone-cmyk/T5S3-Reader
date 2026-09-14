#include <T5AppApi.h>
#include <T5StatusBarApi.h>

#include <HalClock.h>
#include <I18n.h>
#include <cstring>

#include "CrossPointSettings.h"
#include "fontIds.h"

namespace {

enum Item : uint32_t {
  ChapterPageCount = 0,
  BookProgressPercentage,
  ProgressBar,
  ProgressBarThickness,
  Title,
  Battery,
  Clock,
};

const StrId kLabels[T5_STATUS_BAR_ITEM_COUNT] = {
    StrId::STR_CHAPTER_PAGE_COUNT,
    StrId::STR_BOOK_PROGRESS_PERCENTAGE,
    StrId::STR_PROGRESS_BAR,
    StrId::STR_PROGRESS_BAR_THICKNESS,
    StrId::STR_TITLE,
    StrId::STR_BATTERY,
    StrId::STR_CLOCK,
};
const StrId kProgressBar[3] = {StrId::STR_BOOK, StrId::STR_CHAPTER, StrId::STR_HIDE};
const StrId kThickness[3] = {StrId::STR_PROGRESS_BAR_THIN, StrId::STR_PROGRESS_BAR_MEDIUM,
                              StrId::STR_PROGRESS_BAR_THICK};
const StrId kTitle[3] = {StrId::STR_BOOK, StrId::STR_CHAPTER, StrId::STR_HIDE};
const StrId kClock[3] = {StrId::STR_HIDE, StrId::STR_DIR_LEFT, StrId::STR_DIR_RIGHT};

bool active() { return t5_app_get_api(T5_APP_ABI_VERSION) != nullptr; }

void copy(char *dst, size_t cap, const char *src) {
  if (!dst || cap == 0) return;
  std::strncpy(dst, src ? src : "", cap - 1);
  dst[cap - 1] = '\0';
}

uint32_t itemCount() {
  if (!active()) return 0;
  return halClock.isAvailable() ? T5_STATUS_BAR_ITEM_COUNT : (T5_STATUS_BAR_ITEM_COUNT - 1u);
}

bool itemGet(uint32_t index, t5_status_bar_item_t *out) {
  if (!active() || !out || index >= itemCount()) return false;
  *out = {};
  copy(out->label, sizeof(out->label), I18N.get(kLabels[index]));
  switch (index) {
    case ChapterPageCount:
      copy(out->value, sizeof(out->value), I18N.get(SETTINGS.statusBarChapterPageCount ? StrId::STR_SHOW : StrId::STR_HIDE));
      break;
    case BookProgressPercentage:
      copy(out->value, sizeof(out->value), I18N.get(SETTINGS.statusBarBookProgressPercentage ? StrId::STR_SHOW : StrId::STR_HIDE));
      break;
    case ProgressBar:
      copy(out->value, sizeof(out->value), I18N.get(kProgressBar[SETTINGS.statusBarProgressBar % 3]));
      break;
    case ProgressBarThickness:
      copy(out->value, sizeof(out->value), I18N.get(kThickness[SETTINGS.statusBarProgressBarThickness % 3]));
      break;
    case Title:
      copy(out->value, sizeof(out->value), I18N.get(kTitle[SETTINGS.statusBarTitle % 3]));
      break;
    case Battery:
      copy(out->value, sizeof(out->value), I18N.get(SETTINGS.statusBarBattery ? StrId::STR_SHOW : StrId::STR_HIDE));
      break;
    case Clock:
      copy(out->value, sizeof(out->value), I18N.get(kClock[SETTINGS.statusBarClock % 3]));
      break;
    default:
      return false;
  }
  return true;
}

bool itemActivate(uint32_t index) {
  if (!active() || index >= itemCount()) return false;
  switch (index) {
    case ChapterPageCount: SETTINGS.statusBarChapterPageCount = (SETTINGS.statusBarChapterPageCount + 1) % 2; break;
    case BookProgressPercentage:
      SETTINGS.statusBarBookProgressPercentage = (SETTINGS.statusBarBookProgressPercentage + 1) % 2;
      break;
    case ProgressBar: SETTINGS.statusBarProgressBar = (SETTINGS.statusBarProgressBar + 1) % 3; break;
    case ProgressBarThickness:
      SETTINGS.statusBarProgressBarThickness = (SETTINGS.statusBarProgressBarThickness + 1) % 3;
      break;
    case Title: SETTINGS.statusBarTitle = (SETTINGS.statusBarTitle + 1) % 3; break;
    case Battery: SETTINGS.statusBarBattery = (SETTINGS.statusBarBattery + 1) % 2; break;
    case Clock: SETTINGS.statusBarClock = (SETTINGS.statusBarClock + 1) % 3; break;
    default: return false;
  }
  SETTINGS.saveToFile();
  return true;
}

const t5_status_bar_api_v1 api = {
    T5_STATUS_BAR_API_VERSION,
    sizeof(t5_status_bar_api_v1),
    itemCount,
    itemGet,
    itemActivate,
};

}  // namespace

extern "C" const t5_status_bar_api_v1 *t5_status_bar_get_api(uint32_t version) {
  return version == T5_STATUS_BAR_API_VERSION && active() ? &api : nullptr;
}
