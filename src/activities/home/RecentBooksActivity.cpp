#include "RecentBooksActivity.h"

#include <FontCacheManager.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>

#include <algorithm>

#include "MappedInputManager.h"
#include "RecentBooksStore.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {
constexpr int ENTER_DELETE_MODE_MS = 700;
constexpr int DELETE_MODE_OFF = 0;
constexpr int DELETE_MODE_DISPLAY = 1;
constexpr int DELETE_MODE_CONFIRM = 2;
constexpr int RECENT_HELP_TEXT_HEIGHT = 36;
constexpr char UTF8_ELLIPSIS[] = "\xE2\x80\xA6";

void appendTextKey(std::string& key, const std::string& text) {
  if (text.empty()) {
    return;
  }
  key.push_back('\n');
  key += text;
}

void recordUserContentText(FontCacheManager* fcm, const int systemFontId, const char* text) {
  if (fcm == nullptr || text == nullptr || text[0] == '\0') {
    return;
  }
  fcm->recordText(text, BaseTheme::resolveTextFontId(systemFontId, TextRole::UserContent), EpdFontFamily::REGULAR);
}
}  // namespace

int RecentBooksActivity::getPageItems() const {
  const auto pageHeight = renderer.getScreenHeight();
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int helpTextHeight =
      (recentBooks.empty() || confirmingDelete >= DELETE_MODE_DISPLAY) ? 0 : RECENT_HELP_TEXT_HEIGHT;
  const int contentHeight = pageHeight - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing - helpTextHeight;
  return std::max(1, contentHeight / std::max(1, metrics.listWithSubtitleRowHeight));
}

int RecentBooksActivity::getTouchedRecentBookIndex(const int16_t y) const {
  if (recentBooks.empty()) {
    return -1;
  }

  const auto pageHeight = renderer.getScreenHeight();
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int helpTextHeight =
      (recentBooks.empty() || confirmingDelete >= DELETE_MODE_DISPLAY) ? 0 : RECENT_HELP_TEXT_HEIGHT;
  const int contentHeight = pageHeight - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing - helpTextHeight;
  const int rowHeight = std::max(1, metrics.listWithSubtitleRowHeight);
  if (y < contentTop || y >= contentTop + contentHeight) {
    return -1;
  }

  const int pageItems = getPageItems();
  const int row = (y - contentTop) / rowHeight;
  if (row < 0 || row >= pageItems) {
    return -1;
  }

  const size_t pageStartIndex = (selectorIndex / static_cast<size_t>(pageItems)) * static_cast<size_t>(pageItems);
  const size_t touchedIndex = pageStartIndex + static_cast<size_t>(row);
  if (touchedIndex >= recentBooks.size()) {
    return -1;
  }

  return static_cast<int>(touchedIndex);
}

void RecentBooksActivity::loadRecentBooks() {
  recentBooks.clear();
  const auto& books = RECENT_BOOKS.getBooks();
  recentBooks.reserve(books.size());

  for (const auto& book : books) {
    if (!Storage.exists(book.path.c_str())) {
      continue;
    }
    recentBooks.push_back(book);
  }
}

void RecentBooksActivity::removeSelectedRecentBook() {
  if (recentBooks.empty() || selectorIndex >= recentBooks.size()) {
    return;
  }

  RECENT_BOOKS.removeBook(recentBooks[selectorIndex].path);
  recentBooks.erase(recentBooks.begin() + static_cast<int>(selectorIndex));
  if (selectorIndex >= recentBooks.size() && selectorIndex > 0) {
    selectorIndex--;
  }

  confirmingDelete = DELETE_MODE_OFF;
  touchDeleteTriggered = false;
  ignoreNextTouchTap = false;
  lastVisibleTextPrewarmKey.clear();
  requestUpdate();
}

void RecentBooksActivity::onEnter() {
  Activity::onEnter();

  loadRecentBooks();
  selectorIndex = 0;
  confirmingDelete = DELETE_MODE_OFF;
  touchDeleteTriggered = false;
  ignoreNextTouchTap = false;
  lastVisibleTextPrewarmKey.clear();
  requestUpdate();
}

void RecentBooksActivity::onExit() {
  Activity::onExit();
  recentBooks.clear();
  confirmingDelete = DELETE_MODE_OFF;
  touchDeleteTriggered = false;
  ignoreNextTouchTap = false;
  lastVisibleTextPrewarmKey.clear();
}

void RecentBooksActivity::loop() {
  MappedInputManager::TouchPoint touchPoint{};
  unsigned long touchHeldMs = 0;
  if (confirmingDelete == DELETE_MODE_OFF && mappedInput.getTouchHold(touchPoint, touchHeldMs, renderer)) {
    const int touchedIndex = getTouchedRecentBookIndex(touchPoint.y);
    if (touchedIndex >= 0) {
      if (selectorIndex != static_cast<size_t>(touchedIndex)) {
        selectorIndex = static_cast<size_t>(touchedIndex);
        requestUpdate();
      }

      if (touchHeldMs >= ENTER_DELETE_MODE_MS && !touchDeleteTriggered) {
        confirmingDelete = DELETE_MODE_DISPLAY;
        touchDeleteTriggered = true;
        ignoreNextTouchTap = true;
        requestUpdate();
        return;
      }
    }
  } else {
    touchDeleteTriggered = false;
  }

  if (confirmingDelete >= DELETE_MODE_DISPLAY) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      if (confirmingDelete == DELETE_MODE_DISPLAY) {
        confirmingDelete = DELETE_MODE_CONFIRM;
        requestUpdate();
      } else {
        removeSelectedRecentBook();
      }
      return;
    }

    if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      confirmingDelete = DELETE_MODE_OFF;
      requestUpdate();
      return;
    }

    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    if (!recentBooks.empty() && selectorIndex < recentBooks.size()) {
      LOG_DBG("RBA", "Selected recent book: %s", recentBooks[selectorIndex].path.c_str());
      onSelectBook(recentBooks[selectorIndex].path);
      return;
    }
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    onGoHome();
    return;
  }

  if (mappedInput.isPressed(MappedInputManager::Button::Confirm) &&
      mappedInput.getHeldTime() > ENTER_DELETE_MODE_MS) {
    if (!recentBooks.empty()) {
      confirmingDelete = DELETE_MODE_DISPLAY;
      requestUpdate();
    }
    return;
  }

  const int pageItems = getPageItems();
  const int listSize = static_cast<int>(recentBooks.size());

  buttonNavigator.onNextRelease([this, listSize] {
    selectorIndex = ButtonNavigator::nextIndex(static_cast<int>(selectorIndex), listSize);
    requestUpdate();
  });

  buttonNavigator.onPreviousRelease([this, listSize] {
    selectorIndex = ButtonNavigator::previousIndex(static_cast<int>(selectorIndex), listSize);
    requestUpdate();
  });

  buttonNavigator.onNextContinuous([this, listSize, pageItems] {
    selectorIndex = ButtonNavigator::nextPageIndex(static_cast<int>(selectorIndex), listSize, pageItems);
    requestUpdate();
  });

  buttonNavigator.onPreviousContinuous([this, listSize, pageItems] {
    selectorIndex = ButtonNavigator::previousPageIndex(static_cast<int>(selectorIndex), listSize, pageItems);
    requestUpdate();
  });
}

bool RecentBooksActivity::onTouchTap(int16_t, int16_t y) {
  if (ignoreNextTouchTap) {
    ignoreNextTouchTap = false;
    return true;
  }

  if (recentBooks.empty()) {
    return false;
  }

  if (confirmingDelete >= DELETE_MODE_DISPLAY) {
    const int rowHeight = std::max(1, UITheme::getInstance().getMetrics().listWithSubtitleRowHeight);
    const int confirmRowY = renderer.getScreenHeight() / 2;
    if (y >= confirmRowY && y < confirmRowY + rowHeight) {
      if (confirmingDelete == DELETE_MODE_DISPLAY) {
        confirmingDelete = DELETE_MODE_CONFIRM;
        requestUpdate();
      } else {
        removeSelectedRecentBook();
      }
      return true;
    }
    return false;
  }

  const int touchedIndex = getTouchedRecentBookIndex(y);
  if (touchedIndex < 0) {
    return false;
  }

  selectorIndex = static_cast<size_t>(touchedIndex);
  LOG_DBG("RBA", "Touch selected recent book: %s", recentBooks[selectorIndex].path.c_str());
  onSelectBook(recentBooks[selectorIndex].path);
  return true;
}

void RecentBooksActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  const auto& metrics = UITheme::getInstance().getMetrics();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_MENU_RECENT_BOOKS));

  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int helpTextHeight =
      (recentBooks.empty() || confirmingDelete >= DELETE_MODE_DISPLAY) ? 0 : RECENT_HELP_TEXT_HEIGHT;
  const int contentHeight = pageHeight - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing - helpTextHeight;
  const int pageItems = getPageItems();

  std::string visibleTextKey;
  if (!recentBooks.empty()) {
    const size_t pageStartIndex = (selectorIndex / static_cast<size_t>(pageItems)) * static_cast<size_t>(pageItems);
    const size_t pageEndIndex = std::min(recentBooks.size(), pageStartIndex + static_cast<size_t>(pageItems));
    for (size_t i = pageStartIndex; i < pageEndIndex; ++i) {
      appendTextKey(visibleTextKey, recentBooks[i].title);
      appendTextKey(visibleTextKey, recentBooks[i].author);
    }
  }

  if (auto* fcm = renderer.getFontCacheManager();
      fcm != nullptr && visibleTextKey != lastVisibleTextPrewarmKey) {
    fcm->resetRecordedText();
    recordUserContentText(fcm, UI_12_FONT_ID, UTF8_ELLIPSIS);
    recordUserContentText(fcm, UI_10_FONT_ID, UTF8_ELLIPSIS);
    if (!recentBooks.empty()) {
      const size_t pageStartIndex = (selectorIndex / static_cast<size_t>(pageItems)) * static_cast<size_t>(pageItems);
      const size_t pageEndIndex = std::min(recentBooks.size(), pageStartIndex + static_cast<size_t>(pageItems));
      for (size_t i = pageStartIndex; i < pageEndIndex; ++i) {
        recordUserContentText(fcm, UI_12_FONT_ID, recentBooks[i].title.c_str());
        recordUserContentText(fcm, UI_10_FONT_ID, recentBooks[i].author.c_str());
      }
    }
    fcm->prewarmRecordedText();
    lastVisibleTextPrewarmKey = visibleTextKey;
  }

  if (recentBooks.empty()) {
    renderer.drawText(UI_10_FONT_ID, metrics.contentSidePadding, contentTop + 20, tr(STR_NO_RECENT_BOOKS));
  } else if (confirmingDelete >= DELETE_MODE_DISPLAY) {
    GUI.drawHelpText(renderer, Rect{0, pageHeight / 2 - RECENT_HELP_TEXT_HEIGHT, pageWidth, RECENT_HELP_TEXT_HEIGHT},
                     tr(STR_CONFIRM_REMOVE_RECENT_BOOK));
    GUI.drawList(renderer, Rect{0, pageHeight / 2, pageWidth, std::max(1, metrics.listWithSubtitleRowHeight)}, 1, 0,
                 [this](int) { return recentBooks[selectorIndex].title; },
                 [this](int) { return recentBooks[selectorIndex].author; },
                 [this](int) { return UITheme::getFileIcon(recentBooks[selectorIndex].path); }, nullptr, false,
                 TextRole::UserContent);
  } else {
    GUI.drawList(
        renderer, Rect{0, contentTop, pageWidth, contentHeight}, recentBooks.size(), selectorIndex,
        [this](int index) { return recentBooks[index].title; }, [this](int index) { return recentBooks[index].author; },
        [this](int index) { return UITheme::getFileIcon(recentBooks[index].path); }, nullptr, false,
        TextRole::UserContent);
    GUI.drawHelpText(renderer, Rect{0, contentTop + contentHeight, pageWidth, helpTextHeight},
                     tr(STR_HOLD_OPEN_TO_REMOVE));
  }

  const auto backLabel = confirmingDelete >= DELETE_MODE_DISPLAY ? tr(STR_CANCEL) : tr(STR_HOME);
  const auto confirmLabel = recentBooks.empty() ? "" : (confirmingDelete >= DELETE_MODE_DISPLAY ? tr(STR_DELETE) : tr(STR_OPEN));
  const auto labels = mappedInput.mapLabels(backLabel, confirmLabel, tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer(DisplayPresentMode::Balanced);
}
