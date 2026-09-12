#include "MarkdownChapterSelectionActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>

#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"

int MarkdownChapterSelectionActivity::getPageItems() const {
  constexpr int lineHeight = 30;
  const int screenHeight = renderer.getScreenHeight();
  const auto orientation = renderer.getOrientation();
  const bool isPortraitInverted = orientation == GfxRenderer::Orientation::PortraitInverted;
  const int hintGutterHeight = isPortraitInverted ? 50 : 0;
  const int startY = 60 + hintGutterHeight;
  const int availableHeight = screenHeight - startY - lineHeight;
  return std::max(1, availableHeight / lineHeight);
}

int MarkdownChapterSelectionActivity::findChapterIndexForPage(int page) const {
  int found = 0;
  for (size_t i = 0; i < chapters.size(); i++) {
    if (page >= chapters[i].startPage) {
      found = static_cast<int>(i);
    }
  }
  return found;
}

void MarkdownChapterSelectionActivity::onEnter() {
  Activity::onEnter();
  selectorIndex = findChapterIndexForPage(currentPage);
  requestUpdate();
}

void MarkdownChapterSelectionActivity::loop() {
  const int pageItems = getPageItems();
  const int totalItems = static_cast<int>(chapters.size());

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    if (!chapters.empty() && selectorIndex >= 0 && selectorIndex < totalItems) {
      setResult(PageResult{static_cast<uint32_t>(chapters[selectorIndex].startPage)});
      finish();
    }
  } else if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    ActivityResult result;
    result.isCancelled = true;
    setResult(std::move(result));
    finish();
  }

  buttonNavigator.onNextRelease([this, totalItems] {
    selectorIndex = ButtonNavigator::nextIndex(selectorIndex, totalItems);
    requestUpdate();
  });

  buttonNavigator.onPreviousRelease([this, totalItems] {
    selectorIndex = ButtonNavigator::previousIndex(selectorIndex, totalItems);
    requestUpdate();
  });

  buttonNavigator.onNextContinuous([this, totalItems, pageItems] {
    selectorIndex = ButtonNavigator::nextPageIndex(selectorIndex, totalItems, pageItems);
    requestUpdate();
  });

  buttonNavigator.onPreviousContinuous([this, totalItems, pageItems] {
    selectorIndex = ButtonNavigator::previousPageIndex(selectorIndex, totalItems, pageItems);
    requestUpdate();
  });
}

bool MarkdownChapterSelectionActivity::onTouchTap(int16_t, int16_t y) {
  const auto orientation = renderer.getOrientation();
  const bool isPortraitInverted = orientation == GfxRenderer::Orientation::PortraitInverted;
  const int contentY = isPortraitInverted ? 50 : 0;
  const int startY = 60 + contentY;
  constexpr int lineHeight = 30;
  if (y < startY) {
    return false;
  }

  const int pageItems = getPageItems();
  const int row = (y - startY) / lineHeight;
  if (row < 0 || row >= pageItems) {
    return false;
  }

  const int itemIndex = (selectorIndex / pageItems) * pageItems + row;
  if (itemIndex < 0 || itemIndex >= static_cast<int>(chapters.size())) {
    return false;
  }

  selectorIndex = itemIndex;
  setResult(PageResult{static_cast<uint32_t>(chapters[selectorIndex].startPage)});
  finish();
  return true;
}

void MarkdownChapterSelectionActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto pageWidth = renderer.getScreenWidth();
  const auto orientation = renderer.getOrientation();
  const bool isLandscapeCw = orientation == GfxRenderer::Orientation::LandscapeClockwise;
  const bool isLandscapeCcw = orientation == GfxRenderer::Orientation::LandscapeCounterClockwise;
  const bool isPortraitInverted = orientation == GfxRenderer::Orientation::PortraitInverted;
  const int hintGutterWidth = (isLandscapeCw || isLandscapeCcw) ? 30 : 0;
  const int contentX = isLandscapeCw ? hintGutterWidth : 0;
  const int contentWidth = pageWidth - hintGutterWidth;
  const int hintGutterHeight = isPortraitInverted ? 50 : 0;
  const int contentY = hintGutterHeight;
  const int pageItems = getPageItems();
  const int titleX =
      contentX + (contentWidth - renderer.getTextWidth(UI_12_FONT_ID, tr(STR_SELECT_CHAPTER), EpdFontFamily::BOLD)) / 2;
  renderer.drawText(UI_12_FONT_ID, titleX, 15 + contentY, tr(STR_SELECT_CHAPTER), true, EpdFontFamily::BOLD);

  if (chapters.empty()) {
    const int emptyX = contentX + (contentWidth - renderer.getTextWidth(UI_10_FONT_ID, tr(STR_NO_CHAPTERS))) / 2;
    renderer.drawText(UI_10_FONT_ID, emptyX, 120 + contentY, tr(STR_NO_CHAPTERS));
    renderer.displayBuffer();
    return;
  }

  const auto pageStartIndex = selectorIndex / pageItems * pageItems;
  renderer.fillRect(contentX, 60 + contentY + (selectorIndex % pageItems) * 30 - 2, contentWidth - 1, 30);
  for (int i = pageStartIndex; i < static_cast<int>(chapters.size()) && i < pageStartIndex + pageItems; i++) {
    const auto& chapter = chapters[i];
    const char* title = chapter.title.empty() ? tr(STR_UNNAMED) : chapter.title.c_str();
    const TextRole titleRole = chapter.title.empty() ? TextRole::System : TextRole::UserContent;
    BaseTheme::drawTextForRole(renderer, UI_10_FONT_ID, titleRole, contentX + 20,
                               60 + contentY + (i % pageItems) * 30, title, i != selectorIndex);
  }

  if (renderer.getOrientation() != GfxRenderer::LandscapeClockwise) {
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  }

  renderer.displayBuffer();
}
