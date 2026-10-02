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
        self.assertIn("FRAME_BYTES", TEXT)
        self.assertLess(TEXT.index("return true;\n}\nstatic bool transfer_frame"), TEXT.index("return wait_idle();"))

if __name__ == "__main__":
    unittest.main()
