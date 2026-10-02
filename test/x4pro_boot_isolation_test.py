#!/usr/bin/env python3
from pathlib import Path
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
    def test_effective_x4_path_excludes_halsystem_begin(self):
        main = (ROOT / "src/main.cpp").read_text()
        # The headless-core checkpoint has its own earlier setup/loop pair.
        # Inspect the full firmware pair, where the X4 branch actually lives.
        setup = main[main.rindex("void setup()"): main.rindex("void loop()")]
        before, body, after = x4_branch(setup)
        effective = before + body
        self.assertNotIn("HalSystem::begin()", effective)
        self.assertIn("x4DiagnosticSetup()", body)
        self.assertLess(body.index("Serial.begin(115200)"), body.index("x4DiagnosticSetup()"))
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
