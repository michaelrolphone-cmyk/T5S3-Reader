// Frozen verbatim pre-cache oracle from cbf44781cb36c12a716f32fcfbdb31ce95b40342.
// Keep independent of production edits; change only for a deliberate behavior change.
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
  int y = layout.contentTop;
  for (int i = first; i < static_cast<int>(lines.size()) && i < first + visibleLines; ++i) {
    if (!lines[i].empty()) {
      BaseTheme::drawTextForRole(*r, UI_10_FONT_ID, TextRole::UserContent, layout.safeLeft + layout.padding, y,
                                 lines[i].c_str());
    }
    y += lineHeight;
  }

  drawChrome(*r, *in, chrome);
  hitLayout.headerBottom = layout.headerBottom;
  hitLayout.rowTop = layout.contentTop;
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
