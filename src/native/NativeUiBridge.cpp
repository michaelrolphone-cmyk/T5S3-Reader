#include <T5AppApi.h>
#include <T5UiApi.h>

#include <GfxRenderer.h>
#include <HalDisplay.h>

#include <algorithm>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "MappedInputManager.h"
#include "NativeAppHost.h"
#include "activities/ActivityManager.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/ButtonNavigator.h"

namespace {
struct HitLayout {
  int headerBottom = 0;
  int rowTop = 0;
  int rowHeight = 0;
  int pageStart = 0;
  int pageItems = 0;
  int rowCount = 0;
};

HitLayout hitLayout;
std::unique_ptr<ButtonNavigator> navigator;

const char* safe(const char* value) { return value ? value : ""; }

bool active() { return t5_app_get_api(T5_APP_ABI_VERSION) != nullptr; }

GfxRenderer* renderer() {
  if (!active()) return nullptr;
  return &activityManager.nativeAppRenderer();
}

MappedInputManager* input() {
  if (!active()) return nullptr;
  return &activityManager.nativeAppInput();
}

Rect rotatePortraitRectToCurrentOrientation(const Rect& rect, const GfxRenderer& renderer) {
  const int portraitWidth = renderer.getDisplayVisibleWidth();
  const int portraitHeight = renderer.getDisplayVisibleHeight();

  switch (renderer.getOrientation()) {
    case GfxRenderer::Orientation::Portrait:
      return rect;
    case GfxRenderer::Orientation::LandscapeClockwise:
      return Rect(portraitHeight - rect.y - rect.height, rect.x, rect.height, rect.width);
    case GfxRenderer::Orientation::PortraitInverted:
      return Rect(portraitWidth - rect.x - rect.width, portraitHeight - rect.y - rect.height, rect.width, rect.height);
    case GfxRenderer::Orientation::LandscapeCounterClockwise:
      return Rect(rect.y, portraitWidth - rect.x - rect.width, rect.height, rect.width);
  }

  return rect;
}

bool containsPoint(const Rect& rect, int16_t x, int16_t y) {
  return x >= rect.x && x < rect.x + rect.width && y >= rect.y && y < rect.y + rect.height;
}

void drawChrome(GfxRenderer& renderer, MappedInputManager& input, const t5_ui_chrome_t* chrome) {
  if (!chrome) return;
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();
  const int left = metrics.contentSidePadding;
  const int rowWidth = pageWidth - left * 2;

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, safe(chrome->title),
                 chrome->subtitle && chrome->subtitle[0] ? chrome->subtitle : nullptr);

  if (chrome->status && chrome->status[0]) {
    const int statusY = pageHeight - metrics.buttonHintsHeight - 22;
    const std::string status = renderer.truncatedText(SMALL_FONT_ID, chrome->status, rowWidth);
    renderer.drawText(SMALL_FONT_ID, left, statusY, status.c_str());
  }

  const auto labels = input.mapLabels(safe(chrome->back_label), safe(chrome->confirm_label),
                                      safe(chrome->previous_label), safe(chrome->next_label));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}

void renderList(const t5_ui_chrome_t* chrome, const t5_ui_list_row_t* rows, uint32_t rowCount,
                int32_t selectedIndex) {
  auto* r = renderer();
  auto* in = input();
  if (!r || !in || (rowCount && !rows)) return;

  r->clearScreen();
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = r->getScreenWidth();
  const int pageHeight = r->getScreenHeight();
  const int listTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing + 8;
  const Rect content{0, listTop, pageWidth, pageHeight - listTop - metrics.buttonHintsHeight - 8};

  bool hasSubtitle = false;
  bool hasValue = false;
  bool highlightValue = false;
  for (uint32_t i = 0; i < rowCount; ++i) {
    hasSubtitle = hasSubtitle || (rows[i].subtitle && rows[i].subtitle[0]);
    hasValue = hasValue || (rows[i].value && rows[i].value[0]);
    highlightValue = highlightValue || ((rows[i].flags & T5_UI_LIST_HIGHLIGHT_VALUE) != 0);
  }

  std::function<std::string(int)> subtitleFn;
  std::function<std::string(int)> valueFn;
  if (hasSubtitle) subtitleFn = [rows](int i) { return std::string(safe(rows[i].subtitle)); };
  if (hasValue) valueFn = [rows](int i) { return std::string(safe(rows[i].value)); };

  GUI.drawList(*r, content, static_cast<int>(rowCount), selectedIndex,
               [rows](int i) { return std::string(safe(rows[i].title)); }, subtitleFn, nullptr, valueFn,
               highlightValue);
  drawChrome(*r, *in, chrome);

  const int rowHeight = hasSubtitle ? metrics.listWithSubtitleRowHeight : metrics.listRowHeight;
  const int pageItems = std::max(1, content.height / std::max(1, rowHeight));
  const int selected = rowCount ? std::clamp(selectedIndex, 0, static_cast<int32_t>(rowCount) - 1) : 0;
  hitLayout.headerBottom = metrics.topPadding + metrics.headerHeight;
  hitLayout.rowTop = content.y;
  hitLayout.rowHeight = rowHeight;
  hitLayout.pageItems = pageItems;
  hitLayout.pageStart = rowCount ? (selected / pageItems) * pageItems : 0;
  hitLayout.rowCount = static_cast<int>(rowCount);

  (void)presentNativeAppUiFrame();
}

void renderTable(const t5_ui_chrome_t* chrome, const t5_ui_table_column_t* columns, uint32_t columnCount,
                 const t5_ui_table_row_t* rows, uint32_t rowCount, int32_t selectedIndex) {
  auto* r = renderer();
  auto* in = input();
  if (!r || !in || !columns || columnCount == 0 || columnCount > T5_UI_MAX_COLUMNS || (rowCount && !rows)) return;

  r->clearScreen();
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = r->getScreenWidth();
  const int pageHeight = r->getScreenHeight();
  const int left = metrics.contentSidePadding;
  const int tableWidth = pageWidth - left * 2;
  const int tableTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing + 8;
  const int bottom = pageHeight - metrics.buttonHintsHeight - 36;
  const int available = std::max(120, bottom - tableTop);
  const int headerHeight = std::max(30, metrics.listRowHeight);
  const int desiredRowHeight = std::max(30, metrics.listRowHeight);
  const int pageItems = std::max(1, (available - headerHeight) / desiredRowHeight);
  const int selected = rowCount ? std::clamp(selectedIndex, 0, static_cast<int32_t>(rowCount) - 1) : 0;
  const int pageStart = rowCount ? (selected / pageItems) * pageItems : 0;
  const int visibleCount = std::min(static_cast<int>(rowCount) - pageStart, pageItems);
  const int rowHeight = visibleCount > 0 ? std::max(30, (available - headerHeight) / visibleCount) : desiredRowHeight;

  uint32_t totalWeight = 0;
  for (uint32_t c = 0; c < columnCount; ++c) totalWeight += columns[c].weight ? columns[c].weight : 1u;
  std::vector<int> colX(columnCount);
  std::vector<int> colWidth(columnCount);
  int cursor = left;
  for (uint32_t c = 0; c < columnCount; ++c) {
    const uint32_t weight = columns[c].weight ? columns[c].weight : 1u;
    colX[c] = cursor;
    const int width = (c + 1 == columnCount)
                          ? left + tableWidth - cursor
                          : static_cast<int>((static_cast<int64_t>(tableWidth) * weight) / totalWeight);
    colWidth[c] = std::max(1, width);
    cursor += colWidth[c];
  }

  for (uint32_t c = 0; c < columnCount; ++c) {
    const auto label = r->truncatedText(UI_10_FONT_ID, safe(columns[c].title), std::max(1, colWidth[c] - 6));
    r->drawText(UI_10_FONT_ID, colX[c], tableTop + 8, label.c_str());
  }
  r->drawLine(left, tableTop + headerHeight - 6, left + tableWidth, tableTop + headerHeight - 6, true);

  const int rowsTop = tableTop + headerHeight;
  for (int local = 0; local < visibleCount; ++local) {
    const int index = pageStart + local;
    const int y = rowsTop + local * rowHeight;
    const bool selectedRow = index == selectedIndex;
    if (selectedRow) r->fillRect(left - 4, y, tableWidth + 8, rowHeight - 2, true);
    const bool ink = !selectedRow;

    if (rows[index].flags & T5_UI_TABLE_ROW_FULL_WIDTH) {
      const auto label = r->truncatedText(UI_12_FONT_ID, safe(rows[index].cells[0]), tableWidth - 8);
      r->drawText(UI_12_FONT_ID, left, y + 10, label.c_str(), ink);
      continue;
    }

    for (uint32_t c = 0; c < columnCount; ++c) {
      const auto value = r->truncatedText(UI_10_FONT_ID, safe(rows[index].cells[c]), std::max(1, colWidth[c] - 6));
      r->drawText(UI_10_FONT_ID, colX[c], y + 10, value.c_str(), ink);
    }
  }

  drawChrome(*r, *in, chrome);
  hitLayout.headerBottom = rowsTop;
  hitLayout.rowTop = rowsTop;
  hitLayout.rowHeight = rowHeight;
  hitLayout.pageItems = pageItems;
  hitLayout.pageStart = pageStart;
  hitLayout.rowCount = static_cast<int>(rowCount);
  (void)presentNativeAppUiFrame();
}

void renderTextView(const t5_ui_chrome_t* chrome, const char* text, int32_t scrollFromBottom,
                    t5_ui_text_view_result_t* result) {
  if (result) *result = {};
  auto* r = renderer();
  auto* in = input();
  if (!r || !in) return;

  r->clearScreen();
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = r->getScreenWidth();
  const int pageHeight = r->getScreenHeight();
  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  int contentBottom = pageHeight - metrics.buttonHintsHeight - metrics.verticalSpacing;
  if (chrome && chrome->status && chrome->status[0]) contentBottom -= 26;
  const int maxWidth = pageWidth - metrics.contentSidePadding * 2;
  const int lineHeight = std::max(1, BaseTheme::getLineHeightForRole(*r, UI_10_FONT_ID, TextRole::UserContent));
  const int visibleLines = std::max(1, (contentBottom - contentTop) / lineHeight);

  std::vector<std::string> lines;
  const std::string source = safe(text);
  size_t start = 0;
  while (start <= source.size()) {
    const size_t end = source.find('\n', start);
    const std::string paragraph = source.substr(start, end == std::string::npos ? std::string::npos : end - start);
    if (paragraph.empty()) {
      lines.emplace_back();
    } else {
      auto wrapped = BaseTheme::wrappedTextForRole(*r, UI_10_FONT_ID, TextRole::UserContent,
                                                   paragraph.c_str(), maxWidth, 96);
      if (wrapped.empty()) lines.push_back(paragraph);
      else lines.insert(lines.end(), wrapped.begin(), wrapped.end());
    }
    if (end == std::string::npos) break;
    start = end + 1;
  }
  if (source.empty()) lines.clear();

  const int maxScroll = std::max(0, static_cast<int>(lines.size()) - visibleLines);
  const int scroll = std::clamp(scrollFromBottom, 0, maxScroll);
  const int first = maxScroll - scroll;
  int y = contentTop;
  for (int i = first; i < static_cast<int>(lines.size()) && i < first + visibleLines; ++i) {
    if (!lines[i].empty()) {
      BaseTheme::drawTextForRole(*r, UI_10_FONT_ID, TextRole::UserContent, metrics.contentSidePadding, y,
                                 lines[i].c_str());
    }
    y += lineHeight;
  }

  drawChrome(*r, *in, chrome);
  hitLayout.headerBottom = metrics.topPadding + metrics.headerHeight;
  hitLayout.rowTop = contentTop;
  hitLayout.rowHeight = lineHeight;
  hitLayout.pageItems = visibleLines;
  hitLayout.pageStart = first;
  hitLayout.rowCount = static_cast<int>(lines.size());

  if (result) {
    result->max_scroll_lines = maxScroll;
    result->total_lines = static_cast<uint32_t>(lines.size());
    result->visible_lines = static_cast<uint32_t>(visibleLines);
  }
  (void)presentNativeAppUiFrame();
}

int32_t hitTest(int16_t x, int16_t y) {
  auto* r = renderer();
  if (!r || x < 0 || y < 0 || x >= r->getScreenWidth() || y >= r->getScreenHeight()) return T5_UI_HIT_NONE;
  if (y < hitLayout.headerBottom) return T5_UI_HIT_HEADER;
  if (y < hitLayout.rowTop || hitLayout.rowHeight <= 0 || hitLayout.pageItems <= 0) return T5_UI_HIT_NONE;
  const int local = (y - hitLayout.rowTop) / hitLayout.rowHeight;
  if (local < 0 || local >= hitLayout.pageItems) return T5_UI_HIT_NONE;
  const int index = hitLayout.pageStart + local;
  return index >= 0 && index < hitLayout.rowCount ? index : T5_UI_HIT_NONE;
}

uint8_t eventForButton(MappedInputManager::Button button) {
  using Button = MappedInputManager::Button;
  switch (button) {
    case Button::Back: return T5_UI_EVENT_BACK;
    case Button::Confirm: return T5_UI_EVENT_CONFIRM;
    case Button::Left:
    case Button::Up: return T5_UI_EVENT_PREVIOUS;
    case Button::Right:
    case Button::Down: return T5_UI_EVENT_NEXT;
    default: return T5_UI_EVENT_NONE;
  }
}

bool pollEvent(t5_ui_event_t* event, uint32_t waitMs) {
  if (!event) return false;
  *event = {};
  const t5_app_api_v1* core = t5_app_get_api(T5_APP_ABI_VERSION);
  auto* r = renderer();
  auto* in = input();
  if (!core || !core->poll || !r || !in) return false;

  t5_app_input_t raw{};
  if (!core->poll(&raw, waitMs)) return false;
  if (raw.exit_requested) {
    event->type = T5_UI_EVENT_EXIT;
    return true;
  }

  if (raw.tapped) {
    const auto bounds = GUI.getButtonHintTouchBounds(*r);
    for (size_t i = 0; i < bounds.size(); ++i) {
      const Rect orientedBounds = rotatePortraitRectToCurrentOrientation(bounds[i], *r);
      if (!containsPoint(orientedBounds, raw.touch_x, raw.touch_y)) continue;
      MappedInputManager::Button button;
      if (in->resolveTouchFrontButton(i, button)) {
        event->type = eventForButton(button);
        return true;
      }
    }
    event->type = T5_UI_EVENT_TAP;
    event->touch_x = raw.touch_x;
    event->touch_y = raw.touch_y;
    return true;
  }

  using Button = MappedInputManager::Button;
  if (in->wasPressed(Button::Back)) {
    event->type = T5_UI_EVENT_BACK;
    return true;
  }
  if (in->wasReleased(Button::Confirm)) {
    event->type = T5_UI_EVENT_CONFIRM;
    return true;
  }

  if (!navigator) navigator = std::make_unique<ButtonNavigator>();
  uint8_t navEvent = T5_UI_EVENT_NONE;
  navigator->onNext([&navEvent] { navEvent = T5_UI_EVENT_NEXT; });
  if (navEvent == T5_UI_EVENT_NONE) {
    navigator->onPrevious([&navEvent] { navEvent = T5_UI_EVENT_PREVIOUS; });
  }
  event->type = navEvent;
  return true;
}

int32_t nextIndex(int32_t currentIndex, uint32_t itemCount) {
  return ButtonNavigator::nextIndex(currentIndex, static_cast<int>(itemCount));
}

int32_t previousIndex(int32_t currentIndex, uint32_t itemCount) {
  return ButtonNavigator::previousIndex(currentIndex, static_cast<int>(itemCount));
}

const t5_ui_api_v1 api = {T5_UI_API_VERSION, sizeof(t5_ui_api_v1), renderList, renderTable,
                          hitTest, pollEvent, nextIndex, previousIndex, renderTextView};
}  // namespace

extern "C" const t5_ui_api_v1* t5_ui_get_api(uint32_t apiVersion) {
  if (apiVersion != T5_UI_API_VERSION || !active()) return nullptr;
  auto& in = activityManager.nativeAppInput();
  ButtonNavigator::setMappedInputManager(in);
  navigator = std::make_unique<ButtonNavigator>();
  hitLayout = {};
  return &api;
}
