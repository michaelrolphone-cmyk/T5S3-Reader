#!/usr/bin/env python3
"""Contracts for the native OBJ/STL viewer and GameBoy-derived fast-video path."""

import json
import re
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[2]
APP = (ROOT / "Apps/model_viewer.c").read_text(encoding="utf-8")
MANIFEST = json.loads((ROOT / "Apps/model_viewer.json").read_text(encoding="utf-8"))
VIDEO_API = (ROOT / "lib/NativeApps/include/T5VideoApi.h").read_text(encoding="utf-8")
VIDEO = (ROOT / "src/native/NativeVideoBridge.cpp").read_text(encoding="utf-8")
TAKEOVER = (ROOT / "src/native/NativeHardwareTakeover.cpp").read_text(encoding="utf-8")
LAUNCHER = (ROOT / "lib/NativeApps/src/NativeAppLauncher.c").read_text(encoding="utf-8")
PLATFORMIO = (ROOT / "platformio.ini").read_text(encoding="utf-8")
FILE_BROWSER = (ROOT / "Apps/file_browser.c").read_text(encoding="utf-8")
ASSOCIATIONS = (ROOT / "src/native/FileAssociationRegistry.cpp").read_text(encoding="utf-8")


class ModelViewerContract(unittest.TestCase):
    def test_manifest_and_release_contract(self):
        self.assertEqual(MANIFEST["version"], "1.2.0")
        self.assertEqual(MANIFEST["min_firmware_version"], "1.3.18")
        self.assertEqual(MANIFEST["file_name"], "model_viewer.elf")
        self.assertEqual(MANIFEST["icon"], "solid:f1b2")
        self.assertEqual(set(MANIFEST["supported_file_types"]), {".obj", ".stl"})
        self.assertIn(
            {"capability": "input.touch.raw", "api": ">=1"},
            MANIFEST["requires"],
        )
        match = re.search(r"(?m)^version = ([0-9]+\.[0-9]+\.[0-9]+)$", PLATFORMIO)
        self.assertIsNotNone(match)
        current = tuple(map(int, match.group(1).split(".")))
        minimum = tuple(map(int, MANIFEST["min_firmware_version"].split(".")))
        self.assertGreaterEqual(current, minimum)

    def test_viewer_requests_display_takeover_and_fast_video(self):
        self.assertIn("app_hardware_takeover", APP)
        self.assertIn("T5_HARDWARE_TAKEOVER_DISPLAY", APP)
        self.assertIn("t5_video_get_api(T5_VIDEO_API_VERSION)", APP)
        self.assertIn("T5_VIDEO_FLAG_ONE_IS_BLACK", APP)
        self.assertIn("g_video->submit(0, g_surface.height)", APP)

    def test_file_browser_opens_obj_and_stl_directly_into_viewer(self):
        self.assertEqual(set(MANIFEST["supported_file_types"]), {".obj", ".stl"})
        self.assertIn("readAppManifest(manifestPath", ASSOCIATIONS)
        self.assertIn("for (size_t i = 0; i < types.count; ++i)", ASSOCIATIONS)
        self.assertIn("file_open->handler_count(vfs_path)", FILE_BROWSER)
        self.assertIn("file_open->handler_get(vfs_path, i", FILE_BROWSER)
        self.assertIn("file_open->open_request(vfs_path, handler->app_id", FILE_BROWSER)
        self.assertIn("g_file_open->source_path_get(g_path,sizeof(g_path))", APP)
        self.assertNotIn("dir_open(", APP)

    def test_obj_and_stl_are_stream_parsed(self):
        self.assertIn("mv_load_obj", APP)
        self.assertIn("mv_load_binary_stl", APP)
        self.assertIn("mv_load_ascii_stl", APP)
        self.assertIn("g_storage->stream_open", APP)
        self.assertIn("g_storage->stream_read", APP)
        self.assertIn("g_storage->stream_seek", APP)
        self.assertIn("MV_MAX_TRIANGLES 80000u", APP)

    def test_gestures_are_raw_touch_snapshot_based(self):
        self.assertIn('g_caps->acquire("input.touch.raw"', APP)
        self.assertIn("g_touch->snapshot", APP)
        self.assertIn("now->count==1u", APP)
        self.assertIn("now->count>=2u", APP)
        self.assertIn("g_view.pan_x+=pan_dx", APP)
        self.assertIn("g_view.pan_y+=pan_dy", APP)
        self.assertIn("g_view.zoom=mv_clampf", APP)
        self.assertIn("MV_DOUBLE_TAP_MS", APP)
        self.assertIn("mv_reset_view()", APP)

    def test_rendering_uses_gameboy_panel_mapping_and_dynamic_lod(self):
        self.assertIn("const int panel_x = y;", APP)
        self.assertIn("const int panel_y = (MV_LOGICAL_W - 1) - x;", APP)
        self.assertIn("MV_INTERACTIVE_TRI_BUDGET 4500u", APP)
        self.assertIn("MV_REFINED_TRI_BUDGET 24000u", APP)
        self.assertIn("mv_render(true)", APP)
        self.assertIn("mv_render(false)", APP)

    def test_video_api_is_bounded_and_takeover_only(self):
        self.assertIn("#define T5_VIDEO_API_VERSION 1u", VIDEO_API)
        self.assertIn("uint8_t *(*backbuffer)", VIDEO_API)
        self.assertIn("bool (*submit)", VIDEO_API)
        self.assertIn("void (*stop)", VIDEO_API)
        self.assertIn("native_hardware_display_is_borrowed()", VIDEO)
        self.assertIn("nativeVideoForceStop();", TAKEOVER)
        self.assertIn("ESP_ELFSYM_EXPORT(t5_video_get_api)", LAUNCHER)

    def test_fast_video_reuses_gameboy_scan_architecture(self):
        self.assertIn("#define TARGET_FPS 24", VIDEO)
        self.assertIn("uint8_t *g_buffers[2]", VIDEO)
        self.assertIn("g_state_buffer", VIDEO)
        self.assertIn("esp_lcd_new_i80_bus", VIDEO)
        self.assertIn("esp_lcd_panel_io_tx_color", VIDEO)
        self.assertIn("xTaskCreatePinnedToCore", VIDEO)
        self.assertIn("scan_task", VIDEO)
        self.assertIn("Board::ScopedI2CLock", VIDEO)
        self.assertIn("GameBoy-derived raw EPD video", VIDEO)

    def test_video_teardown_releases_dma_and_lcd_bus(self):
        self.assertIn("esp_lcd_panel_io_del", VIDEO)
        self.assertIn("esp_lcd_del_i80_bus", VIDEO)
        self.assertIn("release_allocations();", VIDEO)
        self.assertIn("g_scan_task != nullptr", VIDEO)


if __name__ == "__main__":
    unittest.main()
