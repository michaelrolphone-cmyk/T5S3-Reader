#!/usr/bin/env python3
"""Exercise verbatim production wrapping/truncation with deterministic font metrics.

No display or SD font hardware is simulated. Utf8.cpp is linked unchanged.
"""
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(os.environ.get("WRAP_TEST_ROOT", Path(__file__).resolve().parents[1]))
source = (ROOT / "src/components/themes/BaseTheme.cpp").read_text()

def function(signature):
    start = source.index(signature)
    brace = source.index("{", start)
    depth = 1
    end = brace + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]

prefix = r'''
#include <cassert>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>
#include "Utf8.h"
struct EpdFontFamily { enum Style { REGULAR, BOLD }; };
enum class TextRole { System, UserContent };
struct GfxRenderer {
  bool sd = false;
  mutable int prepared = 0;
  bool isSdCardFont(int) const { return sd; }
  void ensureSdCardFontReady(int, const char*, uint8_t) const { ++prepared; }
  int getTextWidth(int, const char* s, EpdFontFamily::Style) const {
    int width = 0;
    auto p = reinterpret_cast<const unsigned char*>(s);
    while (*p) { utf8NextCodepoint(&p); ++width; }
    return width;
  }
  int getTextAdvanceX(int f, const char* s, EpdFontFamily::Style st) const {
    return getTextWidth(f, s, st);
  }
};
struct BaseTheme {
  static int resolveTextFontId(int f, TextRole) { return f; }
  static std::vector<std::string> wrappedTextForRole(const GfxRenderer&, int,
      TextRole, const char*, int, int, EpdFontFamily::Style);
};
'''
body = "\n".join(function(s) for s in (
    "uint8_t styleMaskForStyle(", "void ensureRoleTextReady(",
    "int measureRoleTextWidth(", "std::string truncatedPreparedText(",
    "std::vector<std::string> BaseTheme::wrappedTextForRole("))
tests = r'''
int main() {
  for (bool sd : {false, true}) {
    GfxRenderer r; r.sd = sd;
    auto wrap = [&](const char* s, int w = 8, int n = 8) {
      return BaseTheme::wrappedTextForRole(r, 1, TextRole::UserContent, s, w, n,
                                         EpdFontFamily::REGULAR);
    };
    assert((wrap("one two three") == std::vector<std::string>{"one two", "three"}));
    assert(wrap(nullptr).empty());
    assert(wrap("").empty());
    assert(wrap("text", 0).empty());
    assert(wrap("text", 8, 0).empty());
    assert(wrap("text", -1).empty());
    assert(wrap("text", 8, -1).empty());
    const auto oversized = wrap("abcdefghijkl");
    assert(oversized.size() == 1 && oversized[0].find("…") != std::string::npos);
    const auto actual = wrap("abcdefghijkl tail words");
    if (actual != std::vector<std::string>{oversized[0], "tail", "words"}) {
      std::cerr << "long leading token lost following words; got " << actual.size() << " line(s)\n";
      return 1;
    }
    assert((wrap("ok abcdefghijkl tail") == std::vector<std::string>{"ok", oversized[0], "tail"}));
    assert((wrap("abcdefghijkl mnopqrstuvwx tail") == std::vector<std::string>{oversized[0], "mnopqr…", "tail"}));
    assert(wrap("abcdefghijkl tail words", 8, 1).size() == 1);
    assert((wrap("abcdefghijkl tail words", 8, 2) == std::vector<std::string>{oversized[0], "tail w…"}));
    assert(wrap("ok abcdefghijkl tail", 8, 2).size() == 2);
    auto unicode = wrap("éééééééééé tail");
    assert((unicode == std::vector<std::string>{"éééééé…", "tail"}));
    assert((wrap("abcdefghijkl   tail ") == std::vector<std::string>{oversized[0], "tail"}));
    assert(wrap("abcdefghijkl tail", 1, 2).size() == 2);
    // Invalid/empty calls cannot retain state or prevent a subsequent retry.
    for (int i = 0; i < 20; ++i) {
      assert(wrap(nullptr).empty());
      assert(wrap("retry works", 8, 8).size() == 2);
      assert(wrap("abcdefghijkl tail", 8, 8).back() == "tail");
    }
    assert(!sd || r.prepared > 0);
  }
  std::cout << "production wrapping regression PASS (built-in/SD metrics, UTF-8, budgets, retry)\n";
}
'''
with tempfile.TemporaryDirectory(prefix="text-wrap-") as temp:
    cpp = Path(temp) / "test.cpp"
    cpp.write_text(prefix + body + tests)
    binary = Path(temp) / "test"
    flags = [os.environ.get("CXX", "c++"), "-std=c++17", "-Wall", "-Wextra", "-Werror"]
    if os.environ.get("WRAP_TEST_SANITIZE") == "1":
        flags += ["-fsanitize=address,undefined", "-fno-sanitize-recover=all"]
    subprocess.run(flags + ["-I" + str(ROOT / "lib/Utf8"), str(cpp),
                           str(ROOT / "lib/Utf8/Utf8.cpp"), "-o", str(binary)], check=True, timeout=60)
    subprocess.run([str(binary)], check=True, timeout=30)
