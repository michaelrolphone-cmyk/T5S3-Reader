#!/usr/bin/env python3
"""Compile production card code and validate decoded grayscale pixels, not hardware."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]

class ReadingCardTests(unittest.TestCase):
    def test_home_uses_gray_present_after_menu_and_hints(self):
        home = (ROOT / "src/activities/home/HomeActivity.cpp").read_text()
        render = home.split("void HomeActivity::render(RenderLock&&)", 1)[1].split("void HomeActivity::onSelectBook", 1)[0]
        self.assertEqual(render.count("HomeReadingCard::present("), 1)
        self.assertLess(render.index("HomeReadingCard::draw("), render.index("GUI.drawButtonMenu("))
        self.assertLess(render.index("GUI.drawButtonHints("), render.index("HomeReadingCard::present("))
        self.assertNotIn("renderer.displayBuffer();", render)

    def test_readiness_query_in_real_headers_for_both_boards(self):
        with tempfile.TemporaryDirectory(prefix="riscrte-gray-readiness-") as temp:
            work = Path(temp)
            (work / "Arduino.h").write_text("#pragma once\n#include <cstdint>\n")
            (work / "Board.h").write_text("#pragma once\nnamespace BoardPins { constexpr int LogicalWidth=540, LogicalHeight=960, DisplayWidth=960, DisplayHeight=540; }\n")
            (work / "EpdFontFamily.h").write_text("#pragma once\nstruct EpdGlyph {}; struct EpdFontData {}; struct EpdFontFamily { enum Style { REGULAR, BOLD }; };\n")
            (work / "Bitmap.h").write_text("#pragma once\nclass Bitmap {};\n")
            # Compile byte-identical production headers in the isolated fixture.
            # Quoted Bitmap.h includes otherwise select the source directory's
            # real SD implementation before -I fixtures in a complete checkout.
            for path in ("lib/GfxRenderer/GfxRenderer.h", "lib/hal/HalDisplay.h"):
                (work / Path(path).name).write_bytes((ROOT / path).read_bytes())
            (work / "test.cpp").write_text(r"""
#include <cassert>
#include <cstring>
#include <map>
#include <string>
#include <vector>
#define private public
#include "GfxRenderer.h"
#include "HalDisplay.h"
#undef private
HalDisplay::HalDisplay() = default;
HalDisplay::~HalDisplay() = default;
uint8_t* HalDisplay::getFrameBuffer() const { return nullptr; }
void HalDisplay::clearScreen(uint8_t) const {}
void HalDisplay::drawImage(const uint8_t*, uint16_t, uint16_t, uint16_t, uint16_t, bool) const {}
void HalDisplay::drawImageTransparent(const uint8_t*, uint16_t, uint16_t, uint16_t, uint16_t, bool) const {}
void HalDisplay::displayBuffer(RefreshMode, bool) {}
void HalDisplay::requestNextRefresh(RefreshMode) {}
void HalDisplay::requestNextDisplayEffect(DisplayEffect) {}
void HalDisplay::copyGrayscaleLsbBuffers(const uint8_t*) {}
void HalDisplay::copyGrayscaleMsbBuffers(const uint8_t*) {}
bool HalDisplay::captureGrayscaleBaseBuffer(const uint8_t*) { return false; }
void HalDisplay::cleanupGrayscaleBuffers(const uint8_t*) {}
void HalDisplay::displayGrayBuffer(RefreshMode) {}
void GfxRenderer::freeBwBufferChunks() {}
int main() {
  HalDisplay display;
  GfxRenderer renderer(display);
  uint8_t sample = 0;
  for (unsigned mask = 0; mask < 16; ++mask) {
    display.grayscaleBaseCaptured = (mask & 1) != 0;
    display.grayscaleBaseBuffer = (mask & 2) ? &sample : nullptr;
    display.grayscaleLsbBuffer = (mask & 4) ? &sample : nullptr;
    display.grayscaleMsbBuffer = (mask & 8) ? &sample : nullptr;
    assert(display.grayscaleBuffersReady() == (mask == 15));
    assert(renderer.grayscaleBuffersReady() == (mask == 15));
  }
}
""")
            for board in ("BOARD_T5S3_PRO", "BOARD_LILYGO_EPD47_S3"):
                executable = work / board
                subprocess.run([os.environ.get("CXX", "g++"), "-std=c++17", "-Wall", "-Wextra", "-Werror",
                                "-D" + board, "-I" + str(work), "-I" + str(ROOT / "lib/GfxRenderer"),
                                "-I" + str(ROOT / "lib/hal"), "-I" + str(ROOT / "lib/DisplaySurface"),
                                str(work / "test.cpp"), "-o", str(executable)], check=True)
                subprocess.run([str(executable)], check=True)

    def test_geometry_and_complete_presentation(self):
        with tempfile.TemporaryDirectory(prefix="riscrte-reading-card-") as temp:
            work = Path(temp)
            for name in ("Bitmap.h", "GfxRenderer.h", "HalStorage.h", "RecentBooksStore.h",
                         "Logging.h", "fontIds.h", "components/UITheme.h", "components/themes/BaseTheme.h"):
                path = work / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text('#include "TestApi.h"\n')
            # Keep the real tr(id) macro: it qualifies a token as StrId::id,
            # unlike a function-shaped fake that would accept a ternary id.
            (work / "I18n.h").write_bytes((ROOT / "lib/I18n/I18n.h").read_bytes())
            (work / "I18nKeys.h").write_text(
                "#pragma once\nenum class StrId { STR_CONTINUE_READING, STR_NO_OPEN_BOOK };\n"
                "enum class Language { EN };\n")
            flags = [os.environ.get("CXX", "g++"), "-std=c++17", "-O2", "-Wall", "-Wextra", "-Werror",
                     "-fsanitize=undefined", "-fno-sanitize-recover=all", "-I" + str(work),
                     "-I" + str(ROOT / "test/reading_card"), "-I" + str(ROOT / "src")]
            for name, sources in (
                ("style", [ROOT / "test/reading_card_style_test.cpp"]),
                ("card", [ROOT / "src/components/HomeReadingCard.cpp", ROOT / "test/reading_card/renderer_test.cpp"]),
            ):
                executable = work / name
                subprocess.run(flags + [str(p) for p in sources] + ["-o", str(executable)], check=True)
                subprocess.run([str(executable)], check=True)

if __name__ == "__main__":
    unittest.main()
