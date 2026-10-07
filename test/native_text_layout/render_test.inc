void compare(const char* text, int scroll, const t5_ui_chrome_t* chrome) {
  t5_ui_text_view_result_t baseline, cached;
  const size_t startingResolverCursor = resolverCursor;
  resolverTrace.clear();
  old::renderTextView(chrome, text, scroll, &baseline);
  const auto expected = frame;
  const auto expectedResolverTrace = resolverTrace;
  const size_t expectedResolverCursor = resolverCursor;
  resolverCursor = startingResolverCursor;
  resolverTrace.clear();
  current::renderTextView(chrome, text, scroll, &cached);
  assert(frame == expected);
  // Selected SD families can load/retry on every resolution, even when the
  // result falls back to a built-in font. Preserve count, order and results.
  assert(resolverCursor == expectedResolverCursor);
  assert(resolverTrace == expectedResolverTrace);
  assert(baseline.max_scroll_lines == cached.max_scroll_lines);
  assert(baseline.total_lines == cached.total_lines);
  assert(baseline.visible_lines == cached.visible_lines);
  assert(old::hitLayout.headerBottom == current::hitLayout.headerBottom);
  assert(old::hitLayout.rowTop == current::hitLayout.rowTop);
  assert(old::hitLayout.rowHeight == current::hitLayout.rowHeight);
  assert(old::hitLayout.pageStart == current::hitLayout.pageStart);
  assert(old::hitLayout.pageItems == current::hitLayout.pageItems);
  assert(old::hitLayout.rowCount == current::hitLayout.rowCount);
}

int main() {
  t5_ui_chrome_t chrome{"title"};
  std::vector<std::string> texts{
      "", "\n", "\n\n", "a\n", "é 中文\n",
      "one two three four five six seven eight nine ten\nfoo\nbar\nbaz"};
  for (const auto& text : texts) {
    current::textViewCache.clear();
    compare(text.c_str(), 0, &chrome);
    const int prior = wraps;
    current::renderTextView(&chrome, text.c_str(), 0, nullptr);
    assert(wraps == prior);
    for (int scroll : {-99, 0, 1, 2, 99999}) compare(text.c_str(), scroll, nullptr);
  }

  std::string mutableText = "one two three four five six seven eight nine ten";
  compare(mutableText.c_str(), 0, &chrome);
  mutableText[0] = 'X';
  compare(mutableText.c_str(), 1, &chrome);
  compare(mutableText.c_str(), 2, &chrome);
  layout.width = 9;
  compare(mutableText.c_str(), 4, &chrome);
  layout.width = 20;
  compare(mutableText.c_str(), 3, &chrome);
  layout.contentBottom = 14;
  compare(mutableText.c_str(), 1, &chrome);
  layout.safeLeft = 20;
  compare(mutableText.c_str(), 0, &chrome);
  r.height = 8;
  compare(mutableText.c_str(), 0, &chrome);
  ++r.generation;
  compare(mutableText.c_str(), 0, &chrome);
  r.fonts[11] = {};
  effectiveFont = 11;
  compare(mutableText.c_str(), 0, &chrome);
  for (bool sd : {true, false}) {
    r.sd = sd;
    compare(mutableText.c_str(), 0, &chrome);
    compare(mutableText.c_str(), 1, &chrome);
  }
  fontData.glyphMissHandler = &r;
  compare(mutableText.c_str(), 1, &chrome);
  assert(current::textViewCache.lineCount() == 0);
  fontData.glyphMissHandler = nullptr;
  r.fonts.clear();
  compare(mutableText.c_str(), 2, &chrome);
  assert(current::textViewCache.lineCount() == 0);
  r.fonts[11] = {};

  for (const auto& large : {std::string(16385, 'a'), std::string(1025, '\n')}) {
    compare(large.c_str(), 0, &chrome);
    assert(current::textViewCache.lineCount() == 0);
    compare(large.c_str(), 2, nullptr);
  }
  current::nativeUiResetTextLayout();
  increment = 101;
  compare(mutableText.c_str(), 1, &chrome);
  assert(current::textViewCache.lineCount() == 0);
  increment = 0;
  compare(mutableText.c_str(), 2, &chrome);

  current::nativeUiResetTextLayout();
  rejectAllocation = true;
  compare(mutableText.c_str(), 1, &chrome);
  assert(current::textViewCache.lineCount() == 0);
  rejectAllocation = false;
  compare(mutableText.c_str(), 1, &chrome);
  assert(current::textViewCache.lineCount() != 0);


  // An unavailable selected SD family can fall back to the ordinary UI font.
  // It must not be cached or cause extra resolver calls from key generation.
  r.sd = false;
  r.fonts[10] = {};
  r.fonts[11] = {};
  layout.width = 22;
  layout.contentBottom = 30;
  r.height = 4;
  const char* resolverText = "a first paragraph that wraps\nsecond paragraph\nthird line";
  compare(resolverText, 0, &chrome);
  assert(current::textViewCache.lineCount() != 0);
  SETTINGS.sdFontFamilyName[0] = 's';
  SETTINGS.sdFontFamilyName[1] = '\0';
  for (const auto& responses : {std::vector<int>{11}, std::vector<int>{0},
                                std::vector<int>{0, 11, 0, 10, 11}}) {
    resolverResults = responses;
    resolverCursor = 0;
    for (int scroll : {0, 1, 2, 0}) {
      compare(resolverText, scroll, &chrome);
      assert(!resolverTrace.empty());
      assert(resolverTrace.front().first == "height");
      for (const auto& call : resolverTrace) assert(call.first != "key");
      assert(current::textViewCache.lineCount() == 0);
    }
  }
  r.sd = true;
  compare(resolverText, 0, &chrome);
  assert(current::textViewCache.lineCount() == 0);
  r.sd = false;
  SETTINGS.sdFontFamilyName[0] = '\0';
  resolverTrace.clear();

  std::mt19937 generator(91);
  for (int i = 0; i < 500; ++i) {
    std::string text;
    for (int n = generator() % 200; n > 0; --n) {
      text += generator() % 5 ? 'a' + generator() % 26 : '\n';
    }
    layout.width = 1 + generator() % 60;
    layout.contentBottom = generator() % 50;
    r.height = 1 + generator() % 8;
    compare(text.c_str(), static_cast<int>(generator() % 100) - 20, &chrome);
    compare(text.c_str(), static_cast<int>(generator() % 100) - 20, nullptr);
  }
  compare(nullptr, 0, nullptr);
  current::nativeUiResetTextLayout();
  assert(current::textViewCache.lineCount() == 0);
  assert(!current::textViewCache.matches(current::textLayoutKey(r, 14), ""));
  compare("new invocation", 0, &chrome);
  current::nativeUiResetTextLayout();
  assert(current::textViewCache.lineCount() == 0);
  active = false;
  compare("irrelevant", 0, nullptr);
  std::cout << "production render review PASS: baseline vs cached draw/chrome/present/result/hit metadata, "
               "500 repeated random cases, mutable input, font/geometry/scroll changes, SD/lazy/missing "
               "font and limit/deadline/allocation fallback, selected-SD resolver sequence parity\n";
}
