#include <T5AppApi.h>
#include <T5UiApi.h>

#include <GfxRenderer.h>
#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <algorithm>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "MappedInputManager.h"
#include "CrossPointSettings.h"
#include "NativeAppHost.h"
#include "NativeTextLayoutCache.h"
#include "NativeUiBridge.h"
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
NativeTextLayoutCache textViewCache;

NativeTextLayoutCache::Key textLayoutKey(const GfxRenderer& renderer, int width) {
  // Resolving a selected SD family may load/retry storage, even when it falls
  // back to a built-in ID. Never add resolver calls or retain that fallback.
  if (SETTINGS.sdFontFamilyName[0] != '\0') return {};
  const int fontId = BaseTheme::resolveTextFontId(UI_10_FONT_ID, TextRole::UserContent);
  if (renderer.isSdCardFont(fontId)) return {};
  const auto font = renderer.getFontMap().find(fontId);
  if (font == renderer.getFontMap().end()) return {};
  const auto* data = font->second.getData(EpdFontFamily::REGULAR);
  // SD/lazy fonts can change as glyphs are loaded or storage is remounted.
  // Their original preparation/measurement/retry path remains authoritative.
  if (!data || data->glyphMissHandler) return {};
  return {&renderer, data, renderer.getFontLayoutGeneration(), fontId, width};
}

struct TextLayoutCopyBudget {
  uint32_t started = millis(), checkpointAt = started;
  size_t bytes = 0, items = 0;
  bool operator()(size_t added) {
    const uint32_t now = millis();
    if (static_cast<uint32_t>(now - started) >= 100) return false;
    bytes += added;
    if (++items >= 64 || bytes >= 4096 || static_cast<uint32_t>(now - checkpointAt) >= 8) {
      vTaskDelay(1);
      checkpointAt = millis();
      bytes = items = 0;
    }
    return static_cast<uint32_t>(millis() - started) < 100;
  }
};

const char* safe(const char* value) { return value ? value : ""; }

const char* listStateIcon(uint8_t flags) {
  if (flags & T5_UI_LIST_ICON_UPDATE) return "solid:f021";
  if (flags & T5_UI_LIST_ICON_INSTALLED) return "regular:f058";
  if (flags & T5_UI_LIST_ICON_DOWNLOAD) return "solid:f019";
  return nullptr;
}

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

struct NativeUiLayout {
  int pageWidth = 0;
  int pageHeight = 0;
  int safeTop = 0;
  int safeRight = 0;
  int safeBottom = 0;
  int safeLeft = 0;
  int padding = 0;
  int spacing = 0;
  int headerTop = 0;
  int headerHeight = 0;
  int headerBottom = 0;
  int footerTop = 0;
  int statusTop = 0;
  int contentTop = 0;
  int contentBottom = 0;
  int statusLineHeight = 0;
  bool compact = false;

  int safeWidth() const { return std::max(1, pageWidth - safeLeft - safeRight); }
  int safeHeight() const { return std::max(1, pageHeight - safeTop - safeBottom); }
};

NativeUiLayout layoutFor(const GfxRenderer& renderer, const t5_ui_chrome_t* chrome) {
  NativeUiLayout layout;
  const auto& metrics = UITheme::getInstance().getMetrics();
  layout.pageWidth = renderer.getScreenWidth();
  layout.pageHeight = renderer.getScreenHeight();
  renderer.getOrientedViewableTRBL(&layout.safeTop, &layout.safeRight, &layout.safeBottom, &layout.safeLeft);
  layout.compact = std::min(layout.safeWidth(), layout.safeHeight()) < 360;
  layout.padding = layout.compact ? std::max(8, metrics.contentSidePadding / 2) : metrics.contentSidePadding;
  layout.spacing = layout.compact ? std::max(4, metrics.verticalSpacing / 2) : metrics.verticalSpacing;
  layout.headerTop = layout.safeTop + metrics.topPadding;
  const int safeBottomY = layout.pageHeight - layout.safeBottom;
  layout.headerHeight = std::min(metrics.headerHeight, std::max(1, safeBottomY - layout.headerTop));
  layout.headerBottom = layout.headerTop + layout.headerHeight;
  layout.footerTop = std::max(layout.headerBottom, safeBottomY - metrics.buttonHintsHeight);

  const bool hasStatus = chrome && chrome->status && chrome->status[0];
  layout.statusLineHeight = hasStatus ? std::max(1, renderer.getLineHeight(SMALL_FONT_ID)) : 0;
  layout.statusTop = hasStatus
                         ? std::max(layout.headerBottom, layout.footerTop - layout.spacing - layout.statusLineHeight)
                         : layout.footerTop;
  layout.contentTop = std::min(layout.footerTop, layout.headerBottom + layout.spacing);
  const int contentLimit = hasStatus ? layout.statusTop - layout.spacing : layout.footerTop - layout.spacing;
  layout.contentBottom = std::max(layout.contentTop, contentLimit);
  return layout;
}

void drawChrome(GfxRenderer& renderer, MappedInputManager& input, const t5_ui_chrome_t* chrome) {
  if (!chrome) return;
  const auto layout = layoutFor(renderer, chrome);
  const int headerWidth = layout.safeWidth();

  GUI.drawHeader(renderer, Rect{layout.safeLeft, layout.headerTop, headerWidth, layout.headerHeight},
                 safe(chrome->title), chrome->subtitle && chrome->subtitle[0] ? chrome->subtitle : nullptr);

  if (chrome->status && chrome->status[0]) {
    const int textX = layout.safeLeft + layout.padding;
    const int textWidth = std::max(1, headerWidth - layout.padding * 2);
    const std::string status = renderer.truncatedText(SMALL_FONT_ID, chrome->status, textWidth);
    renderer.drawText(SMALL_FONT_ID, textX, layout.statusTop, status.c_str());
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

  textViewCache.clear();
  r->clearScreen();
  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto layout = layoutFor(*r, chrome);
  const Rect content{layout.safeLeft, layout.contentTop, layout.safeWidth(),
                     std::max(0, layout.contentBottom - layout.contentTop)};

  bool hasSubtitle = false;
  bool hasValue = false;
  bool highlightValue = false;
  bool hasStateIcon = false;
  bool compactStateIcons = false;
  for (uint32_t i = 0; i < rowCount; ++i) {
    hasSubtitle = hasSubtitle || (rows[i].subtitle && rows[i].subtitle[0]);
    hasValue = hasValue || (rows[i].value && rows[i].value[0]);
    highlightValue = highlightValue || ((rows[i].flags & T5_UI_LIST_HIGHLIGHT_VALUE) != 0);
    const bool rowHasStateIcon = (rows[i].flags & T5_UI_LIST_ICON_MASK) != 0;
    hasStateIcon = hasStateIcon || rowHasStateIcon;
    compactStateIcons = compactStateIcons ||
        (rowHasStateIcon && (rows[i].flags & T5_UI_LIST_ICON_COMPACT) != 0);
  }

  std::function<std::string(int)> subtitleFn;
  std::function<std::string(int)> valueFn;
  std::function<const char*(int)> stateIconFn;
  if (hasSubtitle) subtitleFn = [rows](int i) { return std::string(safe(rows[i].subtitle)); };
  if (hasValue) valueFn = [rows](int i) { return std::string(safe(rows[i].value)); };
  if (hasStateIcon) stateIconFn = [rows](int i) { return listStateIcon(rows[i].flags); };

  const int stateIconSize = compactStateIcons ? 12 : 16;
  GUI.drawList(*r, content, static_cast<int>(rowCount), selectedIndex,
               [rows](int i) { return std::string(safe(rows[i].title)); }, subtitleFn, nullptr, valueFn,
               highlightValue, TextRole::System, stateIconFn, stateIconSize);

  const int rowHeight = hasSubtitle ? metrics.listWithSubtitleRowHeight : metrics.listRowHeight;
  const int pageItems = std::max(1, content.height / std::max(1, rowHeight));
  const int selected = rowCount ? std::clamp(selectedIndex, 0, static_cast<int32_t>(rowCount) - 1) : 0;
  const int pageStart = rowCount ? (selected / pageItems) * pageItems : 0;

  drawChrome(*r, *in, chrome);
  hitLayout.headerBottom = layout.headerBottom;
  hitLayout.rowTop = content.y;
  hitLayout.rowHeight = rowHeight;
  hitLayout.pageItems = pageItems;
  hitLayout.pageStart = pageStart;
  hitLayout.rowCount = static_cast<int>(rowCount);
  (void)presentNativeAppUiFrame();
}

void renderTable(const t5_ui_chrome_t* chrome, const t5_ui_table_column_t* columns, uint32_t columnCount,
                 const t5_ui_table_row_t* rows, uint32_t rowCount, int32_t selectedIndex) {
  auto* r = renderer();
  auto* in = input();
  if (!r || !in || !columns || columnCount == 0 || columnCount > T5_UI_MAX_COLUMNS || (rowCount && !rows)) return;

  textViewCache.clear();
  r->clearScreen();
  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto layout = layoutFor(*r, chrome);
  const int left = layout.safeLeft + layout.padding;
  const int tableWidth = std::max(1, layout.safeWidth() - layout.padding * 2);
  const int tableTop = layout.contentTop;
  const int available = std::max(1, layout.contentBottom - tableTop);
  const int fontLine = std::max(1, r->getLineHeight(UI_10_FONT_ID));
  const int headerHeight = std::min(std::max(24, fontLine + 10), std::max(24, available / 2));
  const int desiredRowHeight = std::max(24, metrics.listRowHeight);
  const int rowSpace = std::max(1, available - headerHeight);
  const int pageItems = std::max(1, rowSpace / desiredRowHeight);
  const int selected = rowCount ? std::clamp(selectedIndex, 0, static_cast<int32_t>(rowCount) - 1) : 0;
  const int pageStart = rowCount ? (selected / pageItems) * pageItems : 0;
  const int visibleCount = std::min(static_cast<int>(rowCount) - pageStart, pageItems);
  const int rowHeight = visibleCount > 0 ? std::max(24, rowSpace / visibleCount) : desiredRowHeight;

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

  const int headerTextY = tableTop + std::max(2, (headerHeight - fontLine) / 2);
  for (uint32_t c = 0; c < columnCount; ++c) {
    const auto label = r->truncatedText(UI_10_FONT_ID, safe(columns[c].title), std::max(1, colWidth[c] - 6));
    r->drawText(UI_10_FONT_ID, colX[c], headerTextY, label.c_str());
  }
  r->drawLine(left, tableTop + headerHeight - 2, left + tableWidth, tableTop + headerHeight - 2, true);

  const int rowsTop = tableTop + headerHeight;
  for (int local = 0; local < visibleCount; ++local) {
    const int index = pageStart + local;
    const int y = rowsTop + local * rowHeight;
    const bool selectedRow = index == selectedIndex;
    if (selectedRow) r->fillRect(left - 4, y, tableWidth + 8, std::max(1, rowHeight - 2), true);
    const bool ink = !selectedRow;
    const int textY = y + std::max(2, (rowHeight - fontLine) / 2);

    if (rows[index].flags & T5_UI_TABLE_ROW_FULL_WIDTH) {
      const auto label = r->truncatedText(UI_12_FONT_ID, safe(rows[index].cells[0]), tableWidth - 8);
      r->drawText(UI_12_FONT_ID, left, textY, label.c_str(), ink);
      continue;
    }
    for (uint32_t c = 0; c < columnCount; ++c) {
      const auto value = r->truncatedText(UI_10_FONT_ID, safe(rows[index].cells[c]), std::max(1, colWidth[c] - 6));
      r->drawText(UI_10_FONT_ID, colX[c], textY, value.c_str(), ink);
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
  const auto layout = layoutFor(*r, chrome);
  const int maxWidth = std::max(1, layout.safeWidth() - layout.padding * 2);
  const int lineHeight = std::max(1, BaseTheme::getLineHeightForRole(*r, UI_10_FONT_ID, TextRole::UserContent));
  const int visibleLines = std::max(1, (layout.contentBottom - layout.contentTop) / lineHeight);

  const auto key = textLayoutKey(*r, maxWidth);
  const bool reused = textViewCache.matches(key, safe(text));
  std::vector<std::string> lines;
  std::string source;
  if (!reused) {
    textViewCache.clear();
    source = safe(text);
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
  }

  const size_t lineCount = reused ? textViewCache.lineCount() : lines.size();
  const int maxScroll = std::max(0, static_cast<int>(lineCount) - visibleLines);
  const int scroll = std::clamp(scrollFromBottom, 0, maxScroll);
  const int first = maxScroll - scroll;
  int y = layout.contentTop;
  for (int i = first; i < static_cast<int>(lineCount) && i < first + visibleLines; ++i) {
    const char* line = reused ? textViewCache.line(i) : lines[i].c_str();
    if (line[0]) {
      BaseTheme::drawTextForRole(*r, UI_10_FONT_ID, TextRole::UserContent, layout.safeLeft + layout.padding, y,
                                 line);
    }
    y += lineHeight;
  }

  drawChrome(*r, *in, chrome);
  hitLayout.headerBottom = layout.headerBottom;
  hitLayout.rowTop = layout.contentTop;
  hitLayout.rowHeight = lineHeight;
  hitLayout.pageItems = visibleLines;
  hitLayout.pageStart = first;
  hitLayout.rowCount = static_cast<int>(lineCount);

  if (result) {
    result->max_scroll_lines = maxScroll;
    result->total_lines = static_cast<uint32_t>(lineCount);
    result->visible_lines = static_cast<uint32_t>(visibleLines);
  }
  (void)presentNativeAppUiFrame();
  // Admit only after the original frame and chrome have been presented, so
  // optional allocation failure cannot change their existing behavior.
  if (!reused && key.fontGeneration && key == textLayoutKey(*r, maxWidth)) {
    (void)textViewCache.store(key, source, lines, TextLayoutCopyBudget{});
  }
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

bool getViewport(t5_ui_viewport_t* out) {
  if (!out) return false;
  auto* r = renderer();
  if (!r) return false;
  const auto layout = layoutFor(*r, nullptr);
  *out = {};
  out->width = layout.pageWidth;
  out->height = layout.pageHeight;
  out->safe_top = static_cast<int16_t>(layout.safeTop);
  out->safe_right = static_cast<int16_t>(layout.safeRight);
  out->safe_bottom = static_cast<int16_t>(layout.safeBottom);
  out->safe_left = static_cast<int16_t>(layout.safeLeft);
  out->content_padding = static_cast<uint16_t>(layout.padding);
  out->vertical_spacing = static_cast<uint16_t>(layout.spacing);
  out->orientation = layout.pageWidth >= layout.pageHeight ? T5_UI_ORIENTATION_LANDSCAPE : T5_UI_ORIENTATION_PORTRAIT;
  const int shortSide = std::min(layout.safeWidth(), layout.safeHeight());
  out->size_class = shortSide < 360 ? T5_UI_SIZE_COMPACT
                    : shortSide >= 600 ? T5_UI_SIZE_EXPANDED
                                       : T5_UI_SIZE_REGULAR;
  return true;
}

const t5_ui_api_v1 api = {T5_UI_API_VERSION, sizeof(t5_ui_api_v1), renderList, renderTable,
                          hitTest, pollEvent, nextIndex, previousIndex, renderTextView, getViewport};
}  // namespace

void nativeUiResetTextLayout() { textViewCache.clear(); }

extern "C" const t5_ui_api_v1* t5_ui_get_api(uint32_t apiVersion) {
  if (apiVersion != T5_UI_API_VERSION || !active()) return nullptr;
  nativeUiResetTextLayout();
  auto& in = activityManager.nativeAppInput();
  ButtonNavigator::setMappedInputManager(in);
  navigator = std::make_unique<ButtonNavigator>();
  hitLayout = {};
  return &api;
}
