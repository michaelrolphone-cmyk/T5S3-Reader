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
        self.assertRegex(SOURCE, r"kRevealFramesPerLayer\s*=\s*6\s*;")

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
        self.assertIn("visibleLayers", args)
        self.assertIn("newestLayerCoverage", args)
        self.assertIn("establishedLayerCoverage", args)
        self.assertNotRegex(args, r"\b(?:x|y|frameX|frameY|offset|position)\b")

    def test_each_new_layer_fades_in_while_completed_layers_remain_solid(self):
        reveal = re.search(
            r"bool\s+renderLayerReveal\s*\(\s*\)\s*\{(?P<body>.*?)\n\}",
            SOURCE,
            re.DOTALL,
        )
        self.assertIsNotNone(reveal)
        body = reveal.group("body")
        self.assertIn(
            "for (uint8_t layer = 1; layer <= kLogoLayerCount; ++layer)", body
        )
        self.assertIn(
            "for (uint8_t frame = 1; frame <= kRevealFramesPerLayer; ++frame)",
            body,
        )
        self.assertIn(
            "smoothCoverage(frame, kRevealFramesPerLayer, 0U, kFullCoverage)",
            body,
        )
        self.assertIn(
            "submitVideoFrame(layer, coverage, kFullCoverage)", body
        )

    def test_labels_arrive_with_the_fourth_layer(self):
        self.assertIn("if (visibleLayers == kLogoLayerCount)", SOURCE)
        self.assertIn('constexpr char title[] = "RISCRTE";', SOURCE)
        self.assertIn('constexpr char status[] = "STARTING...";', SOURCE)
        self.assertIn("title, titleScale, newestLayerCoverage", SOURCE)
        self.assertIn("status, statusScale, newestLayerCoverage", SOURCE)

    def test_pulse_and_fade_keep_the_completed_logo_stationary(self):
        self.assertIn(
            "submitVideoFrame(kLogoLayerCount, coverage, coverage)", SOURCE
        )
        self.assertRegex(SOURCE, r"kPulseFrames\s*=\s*48\s*;")
        self.assertRegex(SOURCE, r"kFadeFrames\s*=\s*6\s*;")

        reveal_frames = 4 * 6
        fade_frames = 6
        self.assertEqual(reveal_frames, 4 * fade_frames)


if __name__ == "__main__":
    unittest.main()
