#!/usr/bin/env python3
from pathlib import Path
import unittest
TEXT = (Path(__file__).resolve().parents[1] / "Drivers/x4pro_panel/driver.c").read_text()

class PanelSequence(unittest.TestCase):
    def test_pinned_ssd1677_window_and_booster(self):
        self.assertIn("command(0x0C);", TEXT)
        self.assertNotIn("command(0x04);", TEXT)
        self.assertIn("{0x00, 0x00, 0x1F, 0x03}", TEXT)
        self.assertIn("{0xDF, 0x01, 0x00, 0x00}", TEXT)
        self.assertIn("data1(0xDF); data1(0x01);", TEXT)
        self.assertIn("spi_byte((uint8_t)~frame[i])", TEXT)
        self.assertIn("transfer_plane(0x24, deadline_ms)", TEXT)
        self.assertIn("transfer_plane(0x26, deadline_ms)", TEXT)
        self.assertIn("command(0x21); data1(0x40)", TEXT)
        self.assertIn("command(0x3C); data1(0xC0)", TEXT)
        self.assertIn("FRAME_BYTES", TEXT)
        self.assertIn("timeout_ms > 0", TEXT)
        self.assertIn("PRESENT_FAILED", TEXT)

if __name__ == "__main__":
    unittest.main()
