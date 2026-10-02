#!/usr/bin/env python3
from pathlib import Path
import unittest
ROOT = Path(__file__).resolve().parents[1]

class X4BootIsolation(unittest.TestCase):
    def test_x4_setup_does_not_enter_home_or_legacy_buses(self):
        main = (ROOT / "src/main.cpp").read_text()
        setup = main[main.index("void setup()"): main.index("void loop()")]
        x4 = setup.split("#ifdef BOARD_XTEINK_X4_PRO", 1)[1].split("#endif", 1)[0]
        self.assertIn("x4DiagnosticSetup()", x4)
        for banned in ("goHome()", "Wire.begin", "Storage.begin()", "setupDisplayAndFonts()", "HalSystem::begin()"):
            self.assertNotIn(banned, x4)
        loop = main[main.index("void loop()"):]
        x4_loop = loop.split("#ifdef BOARD_XTEINK_X4_PRO", 1)[1].split("#endif", 1)[0]
        self.assertIn("x4DiagnosticLoop()", x4_loop)
        self.assertNotIn("activityManager", x4_loop)

if __name__ == "__main__":
    unittest.main()
