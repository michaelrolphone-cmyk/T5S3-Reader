#!/usr/bin/env python3
from pathlib import Path
import unittest
ROOT = Path(__file__).resolve().parents[1]

class X4BootIsolation(unittest.TestCase):
    def test_effective_x4_path_excludes_halsystem_begin(self):
        main = (ROOT / "src/main.cpp").read_text()
        # The headless-core checkpoint has its own earlier setup/loop pair.
        # Inspect the full firmware pair, where the X4 branch actually lives.
        setup = main[main.rindex("void setup()"): main.rindex("void loop()")]
        before, guarded = setup.split("#ifdef BOARD_XTEINK_X4_PRO", 1)
        body, after = guarded.split("#endif", 1)
        effective = before + body
        self.assertNotIn("HalSystem::begin()", effective)
        self.assertIn("x4DiagnosticSetup()", body)
        self.assertIn("HalSystem::begin()", after)
        for banned in ("goHome()", "Storage.begin()", "setupDisplayAndFonts()"):
            self.assertNotIn(banned, body)
        loop = main[main.rindex("void loop()") :]
        x4_loop = loop.split("#ifdef BOARD_XTEINK_X4_PRO", 1)[1].split("#endif", 1)[0]
        self.assertIn("x4DiagnosticLoop()", x4_loop)
        self.assertNotIn("activityManager", x4_loop)

if __name__ == "__main__":
    unittest.main()
