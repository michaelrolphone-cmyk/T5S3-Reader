#!/usr/bin/env python3
"""Regression contracts for the RiscRTE boot-logo choreography."""

from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[1]
SOURCE = (ROOT / "src/components/StartupScreen.cpp").read_text(encoding="utf-8")


class BootAnimationContract(unittest.TestCase):
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

    def test_logo_geometry_is_centered_and_never_animated(self):
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
            "const int travel",
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

    def test_blocks_pop_in_left_to_right_with_equal_time_per_layer(self):
        reveal = re.search(
            r"bool\s+renderLayerReveal\s*\(\s*\)\s*\{(?P<body>.*?)\n\}",
            SOURCE,
            re.DOTALL,
        )
        self.assertIsNotNone(reveal)
        body = reveal.group("body")
        self.assertIn("for (uint8_t layer = 0; layer < kLogoLayerCount; ++layer)", body)
        self.assertIn("const uint8_t blocksInLayer = lastBlock - firstBlock;", body)
        self.assertIn("for (uint8_t block = firstBlock + 1; block <= lastBlock; ++block)", body)
        self.assertIn("(block - firstBlock) * kRevealLayerMs / blocksInLayer", body)
        self.assertIn("submitBeforeDeadline(block, 0U)", body)
        self.assertIn("submitVideoFrame(visibleBlocks, kFullCoverage, textCoverage, timeout)", body)
        self.assertLess(4 * 250 + 300, 2000)

    def test_labels_fade_only_after_last_block_at_fixed_coordinates(self):
        self.assertIn("if (visibleBlocks == kLogoBlockCount && textCoverage != 0U)", SOURCE)
        reveal = SOURCE[SOURCE.index("bool renderLayerReveal()"):
                        SOURCE.index("bool bootWithVideo(")]
        self.assertLess(reveal.index("firstBlock = lastBlock;"),
                        reveal.index("for (uint8_t frame = 1; frame <= kTextFadeFrames; ++frame)"))
        self.assertIn("submitBeforeDeadline(kLogoBlockCount, coverage)", reveal)
        self.assertIn('constexpr char title[] = "RISCRTE";', SOURCE)
        self.assertIn('constexpr char status[] = "STARTING...";', SOURCE)
        self.assertIn("title, titleScale, textCoverage", SOURCE)
        self.assertIn("status, statusScale, textCoverage", SOURCE)

    def test_pulse_and_fade_keep_the_completed_logo_stationary(self):
        self.assertIn(
            "submitVideoFrame(kLogoBlockCount, coverage, coverage)", SOURCE
        )
        self.assertRegex(SOURCE, r"kPulseFrames\s*=\s*48\s*;")
        self.assertRegex(SOURCE, r"kFadeFrames\s*=\s*6\s*;")



if __name__ == "__main__":
    unittest.main()
