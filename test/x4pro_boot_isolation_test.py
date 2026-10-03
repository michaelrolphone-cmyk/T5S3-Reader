#!/usr/bin/env python3
from pathlib import Path
import re
import unittest
ROOT = Path(__file__).resolve().parents[1]

def x4_branch(source):
    marker = "#ifdef BOARD_XTEINK_X4_PRO"
    before, rest = source.split(marker, 1)
    depth = 1
    body = []
    lines = rest.splitlines(keepends=True)
    for index, line in enumerate(lines):
        directive = line.strip()
        if directive.startswith(("#if ", "#ifdef ", "#ifndef ")):
            depth += 1
        elif directive.startswith("#endif"):
            depth -= 1
            if depth == 0:
                return before, "".join(body), "".join(lines[index + 1:])
        body.append(line)
    raise AssertionError("unterminated X4 branch")

class X4BootIsolation(unittest.TestCase):
    def test_portrait_home_and_distinct_boot_present(self):
        boot = (ROOT / "src/platform/X4DiagnosticBoot.cpp").read_text()
        self.assertIn("renderer.setOrientation(GfxRenderer::Portrait)", boot)
        self.assertIn("renderer.getScreenWidth() != 480 || renderer.getScreenHeight() != 800", boot)
        self.assertLess(boot.index("boot splash present=%d"), boot.index("provider_surface->clearPresentStatus()"))
        self.assertLess(boot.index("provider_surface->clearPresentStatus()"), boot.index("activityManager.goHome()"))
        self.assertIn("menu_height < required_menu_height", boot)

        classic = (ROOT / "src/components/themes/BaseTheme.h").read_text()
        def metric(name):
            match = re.search(r"\." + name + r"\s*=\s*(\d+)", classic)
            self.assertIsNotNone(match, name)
            return int(match.group(1))
        reserved = sum(metric(name) for name in (
            "homeTopPadding", "homeCoverTileHeight", "homeMenuTopOffset", "buttonHintsHeight"))
        rows = 3 * metric("menuRowHeight") + 2 * metric("menuSpacing")
        self.assertGreaterEqual(800 - reserved, rows)
        self.assertLess(480 - reserved, rows)

    def test_effective_x4_path_excludes_halsystem_begin(self):
        main = (ROOT / "src/main.cpp").read_text()
        # The headless-core checkpoint has its own earlier setup/loop pair.
        # Inspect the full firmware pair, where the X4 branch actually lives.
        setup = main[main.rindex("void setup()"): main.rindex("void loop()")]
        before, body, after = x4_branch(setup)
        effective = before + body
        self.assertNotIn("HalSystem::begin()", effective)
        self.assertIn("x4DiagnosticSetup(DeskClockSleep::consumeUserWake())", body)
        self.assertLess(body.index("Serial.begin(115200)"), body.index("x4DiagnosticSetup(DeskClockSleep::consumeUserWake())"))
        self.assertIn("millis() - x4SerialStart < 500", body)
        self.assertIn("HalSystem::begin()", after)
        for banned in ("goHome()", "Storage.begin()", "setupDisplayAndFonts()"):
            self.assertNotIn(banned, body)
        loop = main[main.rindex("void loop()") :]
        _, x4_loop, _ = x4_branch(loop)
        self.assertIn("x4DiagnosticLoop()", x4_loop)
        self.assertNotIn("activityManager", x4_loop)

if __name__ == "__main__":
    unittest.main()
