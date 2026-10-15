"""Compiler discovery is independent of Arduino/PlatformIO frameworks."""
import os
from pathlib import Path
import sys
import tempfile
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
from native_toolchain import compiler_path, TARGET_CC


class NativeToolchainTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.environment = mock.patch.dict(os.environ, {
            "PATH": "", "HOME": str(self.root),
            "PLATFORMIO_CORE_DIR": str(self.root / "legacy"),
        }, clear=True)
        self.environment.start()
        self.addCleanup(self.environment.stop)

    def compiler(self, name, directory="bin"):
        path = self.root / directory / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text("#!/bin/sh\nexit 0\n")
        path.chmod(0o755)
        return path

    def test_exported_target_wrapper_needs_no_framework(self):
        cc = self.compiler(TARGET_CC)
        os.environ["PATH"] = str(cc.parent)
        self.assertEqual(compiler_path(), str(cc))
        self.assertFalse((self.root / "legacy").exists())

    def test_explicit_override_wins_over_path_and_environment(self):
        cc = self.compiler(TARGET_CC, "explicit")
        other = self.compiler(TARGET_CC)
        os.environ.update(PATH=str(other.parent), NATIVE_APP_CC=str(other))
        self.assertEqual(compiler_path(cc), str(cc))

    def test_driver_override_uses_driver_environment(self):
        cc = self.compiler(TARGET_CC)
        os.environ["NATIVE_DRIVER_CC"] = str(cc)
        os.environ["NATIVE_APP_CC"] = "/not-the-driver"
        self.assertEqual(compiler_path(environment="NATIVE_DRIVER_CC"), str(cc))

    def test_explicit_executable_name_resolves_through_path(self):
        cc = self.compiler(TARGET_CC)
        os.environ["PATH"] = str(cc.parent)
        self.assertEqual(compiler_path(TARGET_CC), str(cc))

    def test_invalid_override_never_silently_falls_back(self):
        cc = self.compiler(TARGET_CC)
        os.environ["PATH"] = str(cc.parent)
        with self.assertRaises(FileNotFoundError):
            compiler_path("/invalid-explicit-compiler")

    def test_unified_override_selects_matching_target_wrapper(self):
        generic = self.compiler("xtensa-esp-elf-gcc")
        wrapper = self.compiler(TARGET_CC)
        self.assertEqual(compiler_path(generic), str(wrapper))

    def test_bare_unified_compiler_without_wrapper_is_rejected(self):
        generic = self.compiler("xtensa-esp-elf-gcc")
        with self.assertRaisesRegex(ValueError, "ESP32-S3 wrapper"):
            compiler_path(generic)

    def test_legacy_tool_only_package_remains_supported(self):
        cc = self.compiler(TARGET_CC,
                           "legacy/packages/toolchain-xtensa-esp32s3/bin")
        self.assertEqual(compiler_path(), str(cc))
        self.assertFalse((self.root / "legacy/packages/framework-arduinoespressif32").exists())

    def test_missing_toolchain_does_not_download_or_guess_headers(self):
        with self.assertRaisesRegex(FileNotFoundError, "Export ESP-IDF"):
            compiler_path()

    def test_target_wrapper_symlink_name_is_preserved(self):
        generic = self.compiler("xtensa-esp-elf-gcc")
        wrapper = generic.with_name(TARGET_CC)
        wrapper.symlink_to(generic)
        self.assertEqual(compiler_path(wrapper), str(wrapper))


if __name__ == "__main__":
    unittest.main()
