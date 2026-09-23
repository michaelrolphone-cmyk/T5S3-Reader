#!/usr/bin/env python3
"""Keep Home pinned ELF launches on the same deferred handoff as other launchers."""

from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[2]
SOURCE = (ROOT / "src/activities/home/HomeActivity.cpp").read_text(encoding="utf-8")
HEADER = (ROOT / "src/activities/home/HomeActivity.h").read_text(encoding="utf-8")


class HomeShortcutLaunchContract(unittest.TestCase):
    def test_home_shortcut_launch_is_deferred_out_of_input_dispatch(self):
        self.assertIn("std::string pendingHomeAppArtifact;", HEADER)

        open_start = SOURCE.index("void HomeActivity::onHomeAppOpen(size_t index)")
        open_end = SOURCE.index("\nvoid HomeActivity::onFileBrowserOpen()", open_start)
        open_block = SOURCE[open_start:open_end]
        self.assertIn(
            "pendingHomeAppArtifact = homeApps[index].file_name;",
            open_block,
        )
        self.assertNotIn("runNativeApp(", open_block)
        self.assertNotIn("resolveInstalledAppPath(", open_block)

        loop_start = SOURCE.index("void HomeActivity::loop()")
        touch_start = SOURCE.index("\nbool HomeActivity::onTouchTap", loop_start)
        loop_block = SOURCE[loop_start:touch_start]
        self.assertIn("if (!pendingHomeAppArtifact.empty())", loop_block)
        self.assertIn(
            "resolveInstalledAppPath(artifact.c_str(), resolvedPath)",
            loop_block,
        )
        self.assertIn(
            "runNativeApp(resolvedPath.c_str(), renderer, mappedInput)",
            loop_block,
        )
        self.assertLess(
            loop_block.index("if (!pendingHomeAppArtifact.empty())"),
            loop_block.index("if (appsPending)"),
        )

    def test_pending_shortcut_is_reset_when_home_is_entered(self):
        enter_start = SOURCE.index("void HomeActivity::onEnter()")
        enter_end = SOURCE.index("\nvoid HomeActivity::onExit()", enter_start)
        self.assertIn(
            "pendingHomeAppArtifact.clear();",
            SOURCE[enter_start:enter_end],
        )


if __name__ == "__main__":
    unittest.main()
