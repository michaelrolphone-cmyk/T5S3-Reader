#!/usr/bin/env python3
"""Run the complete production Footnotes activity against input/drawing fixtures.

Only hardware/framework boundaries are stubbed. Activity declarations, every
method, FootnoteEntry and ActivityResult come from the repository. --source can
point at an unchanged baseline .cpp to demonstrate the original failure.
"""
import argparse
import os
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser()
parser.add_argument("--source", type=Path, default=ROOT / "src/activities/reader/EpubReaderFootnotesActivity.cpp")
parser.add_argument("--sanitize", action="store_true")
args = parser.parse_args()


def without_includes(text):
    return re.sub(r"^\s*#(?:include|pragma once)[^\n]*", "", text, flags=re.M)


prefix = r'''
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <iostream>
#include <string>
#include <utility>
#include <vector>
#include "activities/ActivityResult.h"
#include "Epub/FootnoteEntry.h"
enum class EpdFontFamily { BOLD, REGULAR };
constexpr int UI_12_FONT_ID = 12, UI_10_FONT_ID = 10;
enum StrId { STR_FOOTNOTES, STR_NO_FOOTNOTES, STR_BACK, STR_SELECT, STR_LINK };
const char* tr(StrId) { return "label"; }
struct RenderLock {};
struct GfxRenderer {
  enum class Orientation { Portrait, PortraitInverted, LandscapeClockwise, LandscapeCounterClockwise };
  Orientation orientation = Orientation::LandscapeClockwise;
  int width = 960, height = 540;
  struct Row { int y; std::string label; bool selected; };
  std::vector<Row> rows;
  std::vector<std::pair<int, int>> highlights;
  int getScreenWidth() const { return width; }
  int getScreenHeight() const { return height; }
  Orientation getOrientation() const { return orientation; }
  void clearScreen() { rows.clear(); highlights.clear(); }
  int getTextWidth(int, const char*, EpdFontFamily) { return 10; }
  void drawText(int font, int, int y, const char* label, bool ink = true,
                EpdFontFamily = EpdFontFamily::REGULAR) {
    if (font == UI_10_FONT_ID) rows.push_back({y - 4, label, !ink});
  }
  void drawCenteredText(int, int, const char*) {}
  void fillRect(int, int y, int, int h, bool) { highlights.emplace_back(y, h); }
  void displayBuffer() {}
};
struct MappedInputManager {
  enum class Button { Back, Confirm, Up, Down };
  Button event = Button::Down;
  bool released = false;
  bool wasReleased(Button b) const { return released && event == b; }
  struct Labels { const char *btn1, *btn2, *btn3, *btn4; };
  Labels mapLabels(const char* a, const char* b, const char* c, const char* d) { return {a,b,c,d}; }
};
struct ButtonNavigator {
  static MappedInputManager* input;
  template<class F> void onNext(F f) { if (input->wasReleased(MappedInputManager::Button::Down)) f(); }
  template<class F> void onPrevious(F f) { if (input->wasReleased(MappedInputManager::Button::Up)) f(); }
};
MappedInputManager* ButtonNavigator::input;
struct Theme {
  void drawButtonHints(GfxRenderer&, const char*, const char*, const char*, const char*) const {}
};
struct UITheme {
  struct Metrics { int buttonHintsHeight = 40; } metrics;
  static UITheme& getInstance() { static UITheme instance; return instance; }
  const Metrics& getMetrics() const { return metrics; }
  const Theme& getTheme() const { static Theme theme; return theme; }
};
#define GUI UITheme::getInstance().getTheme()
struct Activity {
  GfxRenderer& renderer; MappedInputManager& mappedInput;
  ActivityResult result; bool finished = false;
  Activity(std::string, GfxRenderer& r, MappedInputManager& i) : renderer(r), mappedInput(i) {}
  virtual ~Activity() = default;
  virtual void onEnter() { finished = false; result = ActivityResult{}; }
  virtual void onExit() {}
  virtual void loop() {}
  virtual bool onTouchTap(int16_t, int16_t) { return false; }
  virtual void render(RenderLock&&) {}
  virtual bool isReaderActivity() const { return false; }
  void requestUpdate() {}
  void setResult(ActivityResult&& r) { result = std::move(r); }
  void finish() { finished = true; }
};
'''

checks = r'''
using O = GfxRenderer::Orientation;
using B = MappedInputManager::Button;
void require(bool ok, const char* message) {
  if (!ok) { std::cerr << message << '\n'; std::exit(1); }
}
void press(EpubReaderFootnotesActivity& a, MappedInputManager& input, B b) {
  input.event = b; input.released = true; a.loop(); input.released = false;
  a.render(RenderLock{});
}
void verifyFrame(const GfxRenderer& r, int expected, int top, int bottom) {
  int selected = 0;
  for (const auto& row : r.rows) {
    require(row.y >= top && row.y + 36 <= bottom, "row extends outside the Footnotes viewport");
    if (row.selected) {
      ++selected;
      require(row.label == std::to_string(expected), "wrong selected footnote label");
    }
  }
  require(selected == 1, "selected footnote must remain fully visible");
  for (auto [y, h] : r.highlights)
    require(y >= top && y + h <= bottom, "selection highlight extends outside viewport");
}
std::vector<FootnoteEntry> notes(int n) {
  std::vector<FootnoteEntry> result(n);
  for (int i = 0; i < n; ++i) {
    std::snprintf(result[i].number, sizeof(result[i].number), "%d", i);
    std::snprintf(result[i].href, sizeof(result[i].href), "chapter.xhtml#note%d", i);
  }
  return result;
}
int main() {
  int cases = 0;
  for (O orientation : {O::LandscapeClockwise, O::LandscapeCounterClockwise, O::Portrait, O::PortraitInverted}) {
    for (auto size : {std::pair<int,int>{540,960}, {480,800}, {600,1024}}) {
      GfxRenderer r; r.orientation = orientation;
      const bool landscape = orientation == O::LandscapeClockwise || orientation == O::LandscapeCounterClockwise;
      r.width = landscape ? size.second : size.first;
      r.height = landscape ? size.first : size.second;
      const int top = orientation == O::PortraitInverted ? 110 : 60;
      const int bottom = r.height - (orientation == O::Portrait ? 40 : 0);
      const int capacity = (bottom - top) / 36;
      for (int count : {1, capacity, capacity + 1, 2 * capacity + 3}) {
        auto entries = notes(count);
        MappedInputManager input; ButtonNavigator::input = &input;
        EpubReaderFootnotesActivity a(r, input, entries);
        a.onEnter(); a.render(RenderLock{});
        verifyFrame(r, 0, top, bottom);
        require(r.rows.size() == static_cast<size_t>(std::min(count, capacity)), "wrong visible row count");
        // Complete wrap in both directions, including crossing each page edge.
        for (int i = 1; i <= count; ++i) { press(a,input,B::Down); verifyFrame(r,i%count,top,bottom); }
        for (int i = count - 1; i >= 0; --i) { press(a,input,B::Up); verifyFrame(r,i,top,bottom); }
        for (int i = 1; i < count; ++i) press(a,input,B::Down);
        verifyFrame(r,count-1,top,bottom);
        // Title, partial bottom row, hint area and out-of-screen taps cannot select.
        for (int y : {-1, top - 1, top + capacity * 36, bottom, r.height - 1, r.height}) {
          if (y >= top && y < top + capacity * 36) continue;
          require(!a.onTouchTap(r.width/2,y), "tap outside complete rows selected a footnote");
          require(!a.finished, "invalid tap finished activity");
        }
        // A touch in each complete rendered row returns the displayed target.
        const auto visible = r.rows;
        for (const auto& row : visible) {
          for (int offset : {0,35}) {
            a.finished = false;
            require(a.onTouchTap(r.width/2,row.y+offset), "visible row edge was not selectable");
            require(a.finished, "valid touch did not finish");
            require(std::get<FootnoteResult>(a.result.data).href == "chapter.xhtml#note" + row.label,
                    "touch returned a different target from the rendered row");
          }
        }
        a.onExit(); a.onEnter(); a.render(RenderLock{});
        verifyFrame(r,0,top,bottom);
        press(a,input,B::Up); press(a,input,B::Confirm);
        require(a.finished && std::get<FootnoteResult>(a.result.data).href == entries.back().href,
                "Confirm did not return wrapped selection");
        a.onExit(); a.onEnter(); press(a,input,B::Back);
        require(a.finished && a.result.isCancelled, "Back failed to cancel");
        a.onExit(); a.onEnter(); a.render(RenderLock{});
        verifyFrame(r,0,top,bottom);
        ++cases;
      }
      // Recompute scrolling if the logical display rotates while the list is open.
      auto many = notes(60);
      MappedInputManager rotatedInput; ButtonNavigator::input = &rotatedInput;
      EpubReaderFootnotesActivity rotated(r,rotatedInput,many);
      rotated.onEnter();
      for (int i=1; i<60; ++i) press(rotated,rotatedInput,B::Down);
      const int originalWidth = r.width, originalHeight = r.height;
      r.orientation = landscape ? O::Portrait : O::LandscapeClockwise;
      std::swap(r.width,r.height);
      rotated.render(RenderLock{});
      verifyFrame(r,59,60,r.height - (landscape ? 40 : 0));
      r.orientation = orientation; r.width = originalWidth; r.height = originalHeight;
      auto empty = notes(0);
      MappedInputManager input; ButtonNavigator::input = &input;
      EpubReaderFootnotesActivity a(r,input,empty);
      a.onEnter(); press(a,input,B::Down); press(a,input,B::Up); press(a,input,B::Confirm);
      require(r.rows.empty() && !a.finished && !a.onTouchTap(100,top), "empty list became selectable");
      press(a,input,B::Back); require(a.finished && a.result.isCancelled, "empty list cannot cancel");
    }
  }
  // No complete row fits: draw/hit-test none, keep cancellation available.
  for (O orientation : {O::Portrait, O::PortraitInverted, O::LandscapeClockwise, O::LandscapeCounterClockwise}) {
    GfxRenderer r; r.orientation = orientation; r.height = 90;
    auto entries = notes(2); MappedInputManager input; ButtonNavigator::input = &input;
    EpubReaderFootnotesActivity a(r,input,entries); a.onEnter(); a.render(RenderLock{});
    require(r.rows.empty(), "tiny viewport drew an incomplete row");
    for (int y=0; y<r.height; ++y) require(!a.onTouchTap(100,y), "tiny viewport accepted invisible row");
    press(a,input,B::Back); require(a.finished && a.result.isCancelled, "tiny viewport cannot cancel");
  }
  std::cout << "Footnotes production viewport: " << cases << " orientation/size/list cases, empty/tiny/reopen checks passed\n";
}
'''

header = without_includes((ROOT / "src/activities/reader/EpubReaderFootnotesActivity.h").read_text())
source = without_includes(args.source.read_text())
with tempfile.TemporaryDirectory(prefix="footnotes-viewport-") as td:
    test = Path(td) / "test.cpp"
    binary = Path(td) / "test"
    test.write_text(prefix + header + source + checks)
    flags = ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"] if args.sanitize else []
    # The unchanged baseline has two unused orientation locals in its tap handler.
    if args.source != ROOT / "src/activities/reader/EpubReaderFootnotesActivity.cpp":
        flags.append("-Wno-unused-variable")
    subprocess.run([os.environ.get("CXX", "c++"), "-std=c++17", "-Wall", "-Wextra", "-Werror",
                    *flags, "-I" + str(ROOT / "src"),
                    "-I" + str(ROOT / "lib/Epub"), str(test), "-o", str(binary)], check=True, timeout=60)
    subprocess.run([str(binary)], check=True, timeout=30)
