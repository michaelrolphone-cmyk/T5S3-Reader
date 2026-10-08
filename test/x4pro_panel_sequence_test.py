#!/usr/bin/env python3
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]
DRIVER_DIR = ROOT / "Drivers" / "x4pro_panel"
TEXT = "\n".join(
    (DRIVER_DIR / name).read_text()
    for name in [
        "driver.c",
        "x4pro_panel_core.inc",
        "x4pro_panel_uc_transport_1.inc",
        "x4pro_panel_uc_transport_2.inc",
        "x4pro_panel_uc_transport_3.inc",
        "x4pro_panel_uc_profiles_1.inc",
        "x4pro_panel_uc_profiles_2.inc",
        "x4pro_panel_uc_profiles_3.inc",
        "x4pro_panel_uc_profiles_4.inc",
        "x4pro_panel_ssd1677_1.inc",
        "x4pro_panel_ssd1677_2.inc",
        "x4pro_panel_api_1.inc",
        "x4pro_panel_api_2.inc",
        "x4pro_panel_api_3.inc",
        "x4pro_panel_api_4.inc",
    ]
)


class PanelSequence(unittest.TestCase):
    def test_ssd1677_backend_is_retained(self):
        self.assertIn("bb_command(0x0c)", TEXT)
        self.assertIn("{0x00, 0x00, 0x1f, 0x03}", TEXT)
        self.assertIn("{0xdf, 0x01, 0x00, 0x00}", TEXT)
        self.assertIn("ssd_transfer_plane(0x24, deadline_ms)", TEXT)
        self.assertIn("ssd_transfer_plane(0x26, deadline_ms)", TEXT)

    def test_uc8279_identity_and_safe_geometry(self):
        self.assertIn("ver[2] == 0x68u", TEXT)
        self.assertIn("confirm_ver[i] == ver[i]", TEXT)
        self.assertIn("{0x03, 0x20, 0x02, 0x58}", TEXT)
        self.assertIn("const uint8_t gate_start[] = {0, 0, 0, 0}", TEXT)
        self.assertIn("UC_VISIBLE_OFFSET 120u", TEXT)
        self.assertNotIn("0x3f", TEXT.lower())
        self.assertNotIn("40000000", TEXT)
        self.assertNotIn("80000000", TEXT)
        self.assertNotIn("CMD_TCON", TEXT)
        self.assertNotIn("uc_register1(0x60", TEXT)

    def test_fixed_20mhz_native_transport(self):
        self.assertIn("SPI2_CLOCK_20MHZ", TEXT)
        self.assertIn("iomux_function(X4PRO_PIN_EPD_MOSI, 4u)", TEXT)
        self.assertIn("iomux_function(X4PRO_PIN_EPD_SCLK, 4u)", TEXT)
        self.assertIn("uc_spi_stream", TEXT)
        self.assertIn("UC_SPI_HZ 20000000u", TEXT)

    def test_external_profiles_and_state_machine(self):
        self.assertIn("UC_PROFILE_DIFF_2F", TEXT)
        self.assertIn("UC_PROFILE_DIFF_1F", TEXT)
        self.assertIn("UC_PROFILE_ABS_1F", TEXT)
        self.assertIn("UC_PROFILE_ABS_SETTLE_2F", TEXT)
        self.assertIn("UC_STATE_DIFF_SYNCED", TEXT)
        self.assertIn("UC_STATE_ABS_BURST", TEXT)
        self.assertIn("UC_MAX_ABS_FRAMES 8u", TEXT)
        self.assertIn("UC_MAX_ABS_MS 600u", TEXT)
        self.assertIn("uc_settle_if_needed", TEXT)
        self.assertIn("uc_write_window_plane(0x10, frame", TEXT)
        self.assertIn("uc_upload_lut", TEXT)

    def test_busy_validation_and_fallback(self):
        self.assertIn("UC_MIN_BUSY_MS 8u", TEXT)
        self.assertIn("implausible busy interval", TEXT)
        self.assertIn("busy never asserted", TEXT)
        self.assertIn("busy completion timeout", TEXT)
        self.assertIn("uc_fast_disabled = true", TEXT)
        self.assertIn("uc_otp_clean", TEXT)

    def test_version_and_manifest(self):
        self.assertIn('v=0.2.0', TEXT)
        manifest = (DRIVER_DIR / "manifest.json").read_text()
        self.assertIn('"version": "0.2.0"', manifest)
        self.assertIn('"status": "experimental-unpublished"', manifest)


if __name__ == "__main__":
    unittest.main()
