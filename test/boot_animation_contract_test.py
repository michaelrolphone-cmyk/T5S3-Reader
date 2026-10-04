#!/usr/bin/env python3
"""Regression contracts for the RiscRTE boot-logo choreography."""

from pathlib import Path
import re
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SOURCE = (ROOT / "src/components/StartupScreen.cpp").read_text(encoding="utf-8")


class BootAnimationContract(unittest.TestCase):
    def test_static_driver_wisp_refresh(self):
        subprocess.run([sys.executable, str(ROOT / "test/hal/static_wisp_refresh_test.py")], check=True)

    def test_shared_video_startup(self):
        subprocess.run([sys.executable, str(ROOT / "test/hal/video_start_scrub_test.py")], check=True)

    def test_spatial_scrub_endpoint_coverage(self):
        # Real drive packing and every pixel/arrival, not a separate mock waveform.
        with tempfile.TemporaryDirectory(prefix="boot-scrub-") as temp:
            binary = Path(temp) / "preview"
            subprocess.run(["c++", "-std=c++17", "-O2", "-Wall", "-Wextra", "-Werror",
                            str(ROOT / "scripts/preview_boot_scrub.cpp"), "-o", str(binary)], check=True)
            subprocess.run([str(binary), temp], check=True, timeout=10)

    def test_real_startup_loading_lifecycle(self):
        # Compile production logic, substituting only platform headers/calls.
        with tempfile.TemporaryDirectory(prefix="boot-loading-") as temp:
            path = Path(temp)
            (path / "boot_loading_source.inc").write_text(
                re.sub(r"^#include[^\n]*", "", SOURCE, flags=re.MULTILINE))
            for board in ([], ["-DBOARD_T5S3_PRO"], ["-DBOARD_XTEINK_X4_PRO"]):
                binary = path / "boot-loading"
                subprocess.run(["c++", "-std=c++17", "-Wall", "-Wextra", "-Werror",
                                "-Wno-unused-function", "-pthread", *board,
                                "-I" + str(path), "-I" + str(ROOT / "lib/NativeApps/include"),
                                str(ROOT / "test/boot_loading_test.cpp"), "-o", str(binary)], check=True)
                subprocess.run([str(binary)], check=True, timeout=10)

    def test_loading_precedes_expensive_initialization(self):
        main = (ROOT / "src/main.cpp").read_text()
        # This choreography belongs to the default graphical entrypoint.
        # The explicit headless branch has its own setup/loop and no animation.
        if main.startswith("#if defined(RISCRTE_PROFILE_HEADLESS)\n"):
            main = main.split("#else\n", 1)[1]
        setup = main[main.index("void setup()") : main.index("void loop()")]
        t5 = setup.split("#ifdef BOARD_XTEINK_X4_PRO", 1)[-1]
        t5 = t5.split("#endif", 1)[-1]
        state = main.split("void setupReaderState() {", 1)[1].split("\n}", 1)[0]
        for work in ("sdFontSystem.begin(renderer)", "KOREADER_STORE.loadFromFile()",
                     "OPDS_STORE.loadFromFile()", "APP_STATE.loadFromFile()", "RECENT_BOOKS.loadFromFile()"):
            self.assertIn(work, state)
        for work in ("setupReaderState()", "logPlatformInputHealth()",
                     "mappedInputManager.update()", "activityManager.goHome()"):
            self.assertLess(t5.index("StartupScreen::boot(renderer)"), t5.index(work))
        start = SOURCE[SOURCE.index("bool bootWithVideo("):SOURCE.index("bool finishVideoBoot(")]
        self.assertNotIn("renderLayerReveal()", start)
        self.assertNotIn("kMinimumPulseMs", SOURCE)
        activity = (ROOT / "src/activities/ActivityManager.cpp").read_text()
        self.assertIn("currentActivity && !StartupScreen::isLoading()", activity)
        home = (ROOT / "src/activities/home/HomeActivity.cpp").read_text()
        self.assertIn("if (StartupScreen::isLoading() && !recentsLoaded) loadRecentCovers", home)
        self.assertIn("if (!bootLoading) GUI.fillPopupProgress", home)
        render = activity[activity.index("void ActivityManager::renderTaskLoop()"):activity.index("void ActivityManager::loop()")]
        # Both boards share render/power locking; only startup choreography
        # is conditional. Follow the T5S3 ordering across these guarded calls.
        self.assertEqual(render.count("currentActivity->render(std::move(lock))"), 1)
        t5_render = render
        self.assertLess(t5_render.index("currentActivity->render(std::move(lock))"),
                        t5_render.index("StartupScreen::destinationReady()"))
        self.assertLess(t5_render.index("StartupScreen::finishBoot(renderer)"),
                        t5_render.index("currentActivity->render(std::move(lock))"))

    def test_original_four_logo_layers_are_preserved(self):
        self.assertRegex(SOURCE, r"kLogoLayerCount\s*=\s*4\s*;")
        self.assertRegex(SOURCE, r"kRevealLayerMs\s*=\s*250\s*;")
        self.assertRegex(SOURCE, r"kTextFadeMs\s*=\s*300\s*;")
        self.assertIn("kLogoLayerCount * kRevealLayerMs + kTextFadeMs < kRevealDeadlineMs", SOURCE)
        self.assertIn("kRevealDeadlineMs < 2000", SOURCE)

        layer_ends = re.search(
            r"kLogoLayerEnds\s*\[\s*kLogoLayerCount\s*\]\s*=\s*\{(?P<body>.*?)\};",
            SOURCE,
            re.DOTALL,
        )
        self.assertIsNotNone(layer_ends)
        self.assertEqual(
            [int(value) for value in re.findall(r"\b\d+\b", layer_ends.group("body"))],
            [5, 7, 8, 9],
        )

    def test_logo_anchor_remains_centered(self):
        self.assertIn(
            "const int frameX = (logicalWidth - kLogoSize) / 2;", SOURCE
        )
        self.assertIn(
            "const int frameY = (logicalHeight - kFrameHeight) / 2;", SOURCE
        )

        for forbidden in (
            "renderDropIn",
            "kDropFrames",
            "kDropStartCoverage",
            "kDropEndCoverage",
            "startY = -kFrameHeight",
        ):
            self.assertNotIn(forbidden, SOURCE)

        submit_signature = re.search(
            r"bool\s+submitVideoFrame\s*\((?P<args>.*?)\)\s*\{",
            SOURCE,
            re.DOTALL,
        )
        self.assertIsNotNone(submit_signature)
        args = submit_signature.group("args")
        self.assertIn("visibleBlocks", args)
        self.assertIn("logoCoverage", args)
        self.assertIn("textCoverage", args)
        self.assertNotRegex(args, r"\b(?:x|y|frameX|frameY|offset|position)\b")

    def test_blocks_assemble_left_to_right_on_a_bounded_timeline(self):
        self.assertIn("(block-firstBlock)*kRevealLayerMs/blocksInLayer", SOURCE)
        self.assertIn("elapsed+40u", SOURCE)
        self.assertIn("age>=360u", SOURCE)
        self.assertIn("if (elapsed>=due) visible=block", SOURCE)
        self.assertIn("drawInkSweep(buffer,bufferSize", SOURCE)
        self.assertNotIn("drawBootAccents", SOURCE)
        self.assertIn("if (elapsed>=kRevealDeadlineMs) return false", SOURCE)

    def test_labels_fade_only_after_last_block_at_fixed_coordinates(self):
        self.assertIn("if (visibleBlocks == kLogoBlockCount && textCoverage != 0U)", SOURCE)
        reveal = SOURCE[SOURCE.index("bool renderLayerReveal()"):
                        SOURCE.index("bool bootWithVideo(")]
        self.assertIn("textElapsed=elapsed>kLogoLayerCount*kRevealLayerMs", reveal)
        self.assertIn("smoothCoverage(textFrame,kTextFadeFrames,0,kFullCoverage)", reveal)
        self.assertIn("drawBootWordmark(buffer,bufferSize", SOURCE)
        self.assertIn("visualTimeMs>=1510u", SOURCE)
        self.assertIn("frameY+244,logoCoverage", SOURCE)
        self.assertIn("ditherPixel(x,y,textCoverage)", SOURCE)

    def test_pulse_and_fade_keep_the_completed_logo_stationary(self):
        self.assertIn(
            "submitVideoFrame(lastVisibleBlocks, coverage, textCoverage, kReadyFadeBudgetMs - elapsed, false)", SOURCE
        )
        self.assertRegex(SOURCE, r"kPulseFrames\s*=\s*48\s*;")
        self.assertRegex(SOURCE, r"kFadeFrames\s*=\s*6\s*;")



if __name__ == "__main__":
    unittest.main()
