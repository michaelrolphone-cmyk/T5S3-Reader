#include <GfxRenderer.h>
#include <fontIds.h>
#include <limits>
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#include "builtinFonts/ubuntu_12_regular.h"
#include "builtinFonts/ubuntu_12_bold.h"
#pragma GCC diagnostic pop
#include "theme_metrics.h"
#include "translations.inc"

struct Rect { int x, y, width, height; };
struct UITheme {
  ThemeMetrics metrics = BaseMetrics::values;
  static UITheme& getInstance() { static UITheme instance; return instance; }
  const ThemeMetrics& getMetrics() const { return metrics; }
};
struct GPIO { bool x3 = false; bool deviceIsX3() const { return x3; } } gpio;
const char* tr(const char* text) { return text; }
struct Gui {
  void drawHeader(GfxRenderer&, Rect r, const char* text) {
    record("header", r.x, r.y, r.width, r.height, std::quoted(text));
  }
  void drawTextField(GfxRenderer&, Rect r, int width, bool cursor, int margin, int area) {
    record("field", r.x, r.y, r.width, r.height, width, cursor, margin, area);
  }
  void drawKeyboardKey(GfxRenderer&, Rect r, const char* label, bool selected, const char* secondary,
                       KeyboardKeyType type = KeyboardKeyType::Normal, bool outline = false) {
    record("key", r.x, r.y, r.width, r.height, std::quoted(label ? label : ""), selected,
           std::quoted(secondary ? secondary : ""), static_cast<int>(type), outline);
  }
  void drawButtonHints(GfxRenderer&, const char* a, const char* b, const char* c, const char* d) {
    record("hints", a, b, c, d);
  }
  void drawSideButtonHints(GfxRenderer&, const char* a, const char* b) { record("side-hints", a, b); }
} GUI;
struct MappedInputManager {
  struct Labels { const char *btn1, *btn2, *btn3, *btn4; };
  Labels mapLabels(const char* a, const char* b, const char* c, const char* d) { return {a, b, c, d}; }
};
struct RenderLock {};
struct Activity {
  GfxRenderer& renderer;
  MappedInputManager& mappedInput;
  Activity(const char*, GfxRenderer& r, MappedInputManager& input) : renderer(r), mappedInput(input) {}
  virtual ~Activity() = default;
  virtual void onEnter() {}
  virtual void onExit() {}
  virtual void loop() {}
  virtual bool onTouchTap(int16_t, int16_t) { return false; }
  virtual void render(RenderLock&&) {}
};
using SemaphoreHandle_t = void*;
SemaphoreHandle_t xSemaphoreCreateMutex() { return reinterpret_cast<void*>(1); }
void vSemaphoreDelete(SemaphoreHandle_t) {}
struct KeyboardStateLock { explicit KeyboardStateLock(SemaphoreHandle_t) {} };
struct ButtonNavigator {};
struct risc_text_input_event_v1;
#define private public
#include "keyboard_class.h"
#undef private
const char* const KeyboardEntryActivity::shiftString[2] = {"shift", "SHIFT"};
void KeyboardEntryActivity::onEnter() {}
void KeyboardEntryActivity::onExit() {}
void KeyboardEntryActivity::loop() {}
bool KeyboardEntryActivity::onTouchTap(int16_t, int16_t) { return false; }
#include "keyboard_methods.inc"

static size_t snapshots = 0;
static std::string repeated(const std::string& seed, size_t size) {
  std::string text;
  while (text.size() < size) text += seed;
  text.resize(size);
  return text;
}
static void resetRecords() { records.str(""); records.clear(); cost = {}; }
static int lineWidth(const KeyboardEntryActivity& keyboard) {
  const auto& m = UITheme::getInstance().metrics;
  const int page = keyboard.renderer.width;
  const int available = page - (gpio.x3 ? 2 * m.sideButtonHintsWidth : 0);
  const int margin = (page - available * m.keyboardTextFieldWidthPercent / 100) / 2;
  const int reserve = keyboard.inputType == InputType::Password ?
      std::max(keyboard.renderer.getTextWidth(UI_12_FONT_ID, "[abc]"),
               keyboard.renderer.getTextWidth(UI_12_FONT_ID, "[***]")) + 4 : 0;
  return page - 2 * margin - reserve;
}
static Cost snapshot(KeyboardEntryActivity& keyboard, const std::string& label, bool enforce) {
  resetRecords();
  keyboard.render(RenderLock{});
  const auto renderCost = cost;
  if (keyboard.text.size() <= 1) {
    assert(renderCost.prefixes == 0);
    assert(renderCost.measures >= 2 && renderCost.measures <= 4);
  }
  const int touchHeight = keyboard.measureInputHeightForTouch(lineWidth(keyboard),
      keyboard.renderer.getLineHeight(UI_12_FONT_ID));
  record("touch-height", touchHeight);
  std::cout << "CASE " << label << '\n' << records.str();
  ++snapshots;
#if !defined(BASELINE) || defined(ENFORCE_COST)
  if (enforce) {
    const auto n = keyboard.text.size();
    // Each line measures its full suffix once, optionally scans prefix metrics,
    // then measures the accepted line. Budget quadratic suffix work, not the
    // old cubic sequence of repeatedly shrinking and remeasuring strings.
    if (!(cost.measuredBytes <= n * n / 16 + n * 16 + 256 && cost.glyphs <= n * n / 8 + n * 40 + 512)) {
      std::cerr << label << " cost: measure-bytes=" << cost.measuredBytes << " glyphs=" << cost.glyphs << '\n';
      assert(false && "Keyboard suffix fitting cost regressed");
    }
  }
#else
  (void)enforce;
#endif
  return renderCost;
}

#ifndef BASELINE
static size_t fitChecks = 0;
static void compareFit(const GfxRenderer& renderer, int id, const std::string& text, int width,
                       EpdFontFamily::Style style = EpdFontFamily::REGULAR) {
  size_t expected = 0;
  for (size_t n = 1; n <= text.size(); ++n)
    if (renderer.getTextAdvanceX(id, text.substr(0, n).c_str(), style) <= width) expected = n;
  size_t actual = 123456;
  const bool fitted = renderer.getTextFittingPrefix(id, text.data(), text.size(), width, actual, style);
  if (fitted != (expected != 0) || actual != (expected ? expected : 123456)) {
    std::cerr << "Mismatch font=" << id << " text=" << text << " width=" << width << " expected=" << expected << " actual=" << actual << '\n';
    assert(false);
  }
  ++fitChecks;
}
static void helperChecks(GfxRenderer& renderer, EpdFont& regular) {
  for (int style = 0; style < 8; ++style)
    for (const auto& text : {"AVATAR office ffi ffl Wavy", "x", " ", "0123456789https://reader.test"})
      for (int width = -1; width <= 380; width += 3)
        compareFit(renderer, UI_12_FONT_ID, text, width, static_cast<EpdFontFamily::Style>(style));
  std::vector<EpdGlyph> glyphs(128);
  for (size_t i = 0; i < glyphs.size(); ++i) glyphs[i].advanceX = 11 + (i * 13) % 190;
  glyphs['f'].advanceX = 180; glyphs['x'].advanceX = 250; glyphs['y'].advanceX = 8;
  const EpdUnicodeInterval interval{0, 127, 0};
  const EpdKernClassEntry classes[] = {{'A', 1}, {'V', 2}, {'f', 2}, {'i', 1}, {'x', 2}, {'y', 1}};
  const int8_t kern[] = {-120, 71, -57, 119};
  const EpdLigaturePair ligatures[] = {{('f' << 16) | 'f', 'x'}, {('f' << 16) | 'i', 'y'},
      {('x' << 16) | 'i', 'y'}, {('y' << 16) | 'f', 'x'}};
  EpdFontData custom{}; custom.glyph = glyphs.data(); custom.intervals = &interval; custom.intervalCount = 1;
  custom.kernLeftClasses = custom.kernRightClasses = classes; custom.kernMatrix = kern;
  custom.kernLeftEntryCount = custom.kernRightEntryCount = 6;
  custom.kernLeftClassCount = custom.kernRightClassCount = 2;
  custom.ligaturePairs = ligatures; custom.ligaturePairCount = 4;
  EpdFont synthetic(&custom);
  renderer.fontMap.emplace(3, EpdFontFamily(&regular, &synthetic, &synthetic));
  assert(renderer.getTextAdvanceX(3, "ff", EpdFontFamily::BOLD) > 8);
  assert(renderer.getTextAdvanceX(3, "ffi", EpdFontFamily::BOLD) <= 8);
  for (const auto& text : {"ffi", "AffifififAVxy", "AVAVAV", "xxyfifffi"})
    for (int width = -12; width < 120; ++width)
      for (int style = 0; style < 8; ++style)
        compareFit(renderer, 3, text, width, static_cast<EpdFontFamily::Style>(style));
  uint32_t random = 94731;
  std::vector<std::string> randomSamples;
  for (int sample = 0; sample < 128; ++sample) {
    std::string text;
    for (int n = 0; n < 40; ++n) { random = random * 1664525u + 1013904223u; text += "AVfixy- "[(random >> 24) % 8]; }
    assert(std::find(randomSamples.begin(), randomSamples.end(), text) == randomSamples.end());
    randomSamples.push_back(text);
    for (int width : {-2, 0, 7, 25, 70, 135}) compareFit(renderer, 3, text, width, EpdFontFamily::BOLD);
  }
  const EpdUnicodeInterval sparseIntervals[] = {{32, 63, 0}, {'f', 'i', 32}};
  EpdFontData sparseData = custom;
  sparseData.intervals = sparseIntervals; sparseData.intervalCount = 2;
  EpdFont sparseFont(&sparseData);
  renderer.fontMap.emplace(7, EpdFontFamily(&sparseFont));
  for (const auto& text : {"AVffi", "A-Vxy", "missing glyphs"})
    for (int width = -12; width < 30; ++width) compareFit(renderer, 7, text, width);
  renderer.fontMap.erase(7);
  EpdFontData lazy = custom;
  lazy.glyphMissHandler = [](void*, uint32_t) -> const EpdGlyph* { assert(false && "Speculative glyph callback"); return nullptr; };
  EpdFont lazyFont(&lazy); renderer.fontMap.emplace(4, EpdFontFamily(&lazyFont));
  SdCardFont sd; renderer.sdCardFonts_.emplace(5, &sd);
  renderer.fontMap.emplace(5, EpdFontFamily(&regular));
  EpdFont nullData(nullptr); renderer.fontMap.emplace(6, EpdFontFamily(&nullData));
  const auto rejected = [&](int id, const char* text, size_t size) {
    size_t bytes = 777;
    const auto before = cost;
    assert(!renderer.getTextFittingPrefix(id, text, size, 300, bytes));
    assert(bytes == 777);
    if (id != UI_12_FONT_ID) assert(cost.glyphs == before.glyphs);
    compareFit(renderer, UI_12_FONT_ID, "retry ffi", 70);
  };
  for (int id : {4, 5, 6, 999}) rejected(id, "ffi", 3);
  rejected(UI_12_FONT_ID, nullptr, 3);
  for (const auto& text : {std::string(), std::string(4097, 'a'), std::string("a\0z", 3),
       std::string("abc\n"), std::string("abc\t"), std::string("abc\x7f"), std::string("\xff"),
       std::string("caf\xc3\xa9"), std::string("\xe2\x82"), std::string("\xc0\xaf"), std::string("\xed\xa0\x80")})
    rejected(UI_12_FONT_ID, text.data(), text.size());
  renderer.sdCardFonts_.clear();
  renderer.fontMap.erase(3); renderer.fontMap.erase(4); renderer.fontMap.erase(5); renderer.fontMap.erase(6);
  for (size_t size : {255, 256, 257, 4096}) {
    std::string text(size, 'i'); size_t bytes = 0;
    clockNow = 0; clockStep = 0; yields = 0;
    assert(renderer.getTextFittingPrefix(UI_12_FONT_ID, text.data(), text.size(), 100000, bytes));
    assert(bytes == size && yields == size / 256);
  }
  std::string text(4096, 'i'); size_t bytes = 777;
  clockNow = std::numeric_limits<uint32_t>::max() - 20; clockStep = 1; yields = 0;
  assert(!renderer.getTextFittingPrefix(UI_12_FONT_ID, text.data(), text.size(), 100000, bytes));
  assert(bytes == 777 && yields > 0 && yields < 150);
  clockNow = std::numeric_limits<uint32_t>::max() - 20; clockStep = 1; yields = 0;
  assert(renderer.getTextFittingPrefix(UI_12_FONT_ID, text.data(), 20, 100000, bytes));
  assert(bytes == 20 && yields >= 2);
  clockNow = clockStep = 0;
  compareFit(renderer, UI_12_FONT_ID, "retry after deadline", 90);
  std::cerr << "Exact prefix comparisons=" << fitChecks << "; admission, retry, bounded yields and clock wrap passed\n";
}
#endif

int main() {
  EpdFont regular(&ubuntu_12_regular), bold(&ubuntu_12_bold);
  GfxRenderer renderer;
  renderer.fontMap.emplace(UI_12_FONT_ID, EpdFontFamily(&regular, &bold));
  renderer.fontMap.emplace(SMALL_FONT_ID, EpdFontFamily(&regular));
  MappedInputManager input;
#ifndef BASELINE
  helperChecks(renderer, regular);
#endif
  const std::vector<std::string> texts = {
      repeated("The office offers a familiar view. AVATAR readers type and revise their text. ", 280),
      repeated("https://reader.example.test/catalog/search?query=office&author=AVATAR&lang=en#chapter-", 383),
      repeated("office-AV/", 1024), repeated("office-AV/", 2048), "", "a",
      "Caf\xc3\xa9 a\xcc\x81 \xe5\xa4\xa9\xe5\x9c\xb0", std::string("bad\xff\xe2\x82", 6), std::string("ab\0cd", 5)};
  for (size_t ti = 0; ti < texts.size(); ++ti) for (int width : {400, 540}) {
    renderer.width = width;
    renderer.height = width == 400 ? 800 : 960;
    for (int mode = 0; mode < (ti >= 2 && ti <= 3 ? 1 : 6); ++mode) {
      KeyboardEntryActivity keyboard(renderer, input, "Keyboard regression", texts[ti], 0,
          mode >= 3 ? InputType::Password : (ti == 1 ? InputType::Url : InputType::Text));
      keyboard.cursorPos = mode == 0 ? keyboard.text.size() : (mode == 1 ? 0 : keyboard.text.size() / 2);
      keyboard.cursorMode = mode == 1 || mode == 2 || mode == 4 || mode == 5;
      keyboard.passwordVisible = mode == 5;
      keyboard.togglePos = mode == 3;
      keyboard.hintVisible = mode % 2;
      keyboard.symMode = mode == 1;
      keyboard.urlMode = ti == 1 && mode == 2;
      keyboard.shiftState = mode == 2;
      keyboard.selectedRow = mode == 5 ? 4 : 0;
      keyboard.selectedCol = mode % 5;
      auto& metrics = UITheme::getInstance().metrics;
      metrics = BaseMetrics::values;
      metrics.keyboardCenteredText = mode % 2;
      metrics.keyboardBottomAligned = mode != 2;
      gpio.x3 = mode == 4;
      const std::string label = std::to_string(ti) + "/" + std::to_string(width) + "/" + std::to_string(mode);
      const auto first = snapshot(keyboard, label, ti < 4);
      const auto original = records.str();
      if (ti < 2) {
        snapshot(keyboard, label + "/repeat", true);
        assert(original == records.str());
        keyboard.text.insert(keyboard.cursorPos, "AVffi"); keyboard.cursorPos += 5;
        snapshot(keyboard, label + "/insert", true);
        keyboard.text.erase(keyboard.cursorPos - 5, 5); keyboard.cursorPos -= 5;
        snapshot(keyboard, label + "/erase", true);
        assert(original == records.str());
      }
      if (mode == 0 && ti < 4)
        std::cerr << "bytes=" << texts[ti].size() << " screen=" << width << " render measures=" << first.measures
                  << " measured-bytes=" << first.measuredBytes << " glyphs=" << first.glyphs
                  << " prefixes=" << first.prefixes << '\n';
    }
  }
  UITheme::getInstance().metrics = BaseMetrics::values;
  gpio.x3 = false;
  renderer.width = 400;
  EpdFontData lazyData = ubuntu_12_regular;
  lazyData.glyphMissHandler = [](void*, uint32_t) -> const EpdGlyph* {
    assert(false && "Resident fallback must not invoke an absent-glyph callback"); return nullptr;
  };
  EpdFont lazyFont(&lazyData);
  SdCardFont sd;
  for (const auto& kind : {"lazy", "sd", "unicode", "invalid", "nul"}) {
    std::string text = texts[0];
    if (std::string(kind) == "lazy") renderer.fontMap.at(UI_12_FONT_ID) = EpdFontFamily(&lazyFont);
    if (std::string(kind) == "sd") renderer.sdCardFonts_.emplace(UI_12_FONT_ID, &sd);
    if (std::string(kind) == "unicode") text.insert(150, "caf\xc3\xa9\xe5\xa4\xa9");
    if (std::string(kind) == "invalid") text.insert(150, "\xff\xe2\x82");
    if (std::string(kind) == "nul") text.insert(150, 1, '\0');
    KeyboardEntryActivity keyboard(renderer, input, "Fallback regression", text);
    keyboard.cursorPos = text.size() / 2;
    const auto fallback = snapshot(keyboard, std::string("fallback/") + kind, false);
    std::cerr << "fallback=" << kind << " render measures=" << fallback.measures
              << " measured-bytes=" << fallback.measuredBytes << " prefixes=" << fallback.prefixes << '\n';
    renderer.sdCardFonts_.clear();
    renderer.fontMap.at(UI_12_FONT_ID) = EpdFontFamily(&regular, &bold);
    keyboard.text = texts[0]; keyboard.cursorPos = keyboard.text.size();
    snapshot(keyboard, std::string("retry/") + kind, true);
  }
  for (bool visible : {false, true}) {
    KeyboardEntryActivity password(renderer, input, "Password toggle", "officeAVpassword", 0, InputType::Password);
    password.cursorPos = 6; password.cursorMode = password.togglePos = password.hintVisible = true;
    password.passwordVisible = visible;
    snapshot(password, visible ? "password/toggle-hide" : "password/toggle-show", true);
  }
  // Synthetic near-full width exercises the 4097-byte rejection through both
  // complete callers without making every host baseline run cubic at 4097.
  const std::string oversized(4097, 'i');
  renderer.width = renderer.getTextAdvanceX(UI_12_FONT_ID, oversized.c_str()) - 1;
  UITheme::getInstance().metrics.keyboardTextFieldWidthPercent = 100;
  KeyboardEntryActivity oversizedKeyboard(renderer, input, "4097-byte boundary", oversized);
  oversizedKeyboard.cursorPos = oversized.size();
  const auto oversizedCost = snapshot(oversizedKeyboard, "fallback/4097", false);
#ifndef BASELINE
  assert(oversizedCost.prefixes == 1);
#else
  (void)oversizedCost;
#endif
  std::cerr << "Complete production render/touch-height snapshots=" << snapshots << '\n';
}
