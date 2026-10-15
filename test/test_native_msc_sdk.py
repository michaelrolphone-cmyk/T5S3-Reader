"""Native MSC SDK contracts; optional actual GCC15 + pinned-HAL integration."""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
from native_msc_sdk import IDF_COMMIT, native_headers, validate_compiler

MACROS = "#define __XTENSA_EL__ 1\n#define __XTENSA_WINDOWED_ABI__ 1\n"


class SdkContracts(unittest.TestCase):
    def check_cc(self, *answers, native=True):
        with patch("native_msc_sdk.subprocess.check_output", side_effect=answers):
            validate_compiler("test-cc", native=native)

    def test_native_compiler(self):
        self.check_cc(MACROS, "xtensa-esp-elf", "15.2.0", "gcc esp-15.2.0_20251204")

    def test_legacy_compiler(self):
        self.check_cc(MACROS, native=False)

    def test_reject_bare_unified_endian(self):
        with self.assertRaisesRegex(ValueError, "little-endian"):
            self.check_cc("#define __XTENSA_EB__ 1\n")

    def test_reject_call0(self):
        with self.assertRaisesRegex(ValueError, "windowed"):
            self.check_cc("#define __XTENSA_EL__ 1\n")

    def test_reject_native_wrong_machine(self):
        with self.assertRaisesRegex(ValueError, "pinned"):
            self.check_cc(MACROS, "xtensa-esp32s3-elf")

    def test_reject_native_wrong_version(self):
        with self.assertRaisesRegex(ValueError, "pinned"):
            self.check_cc(MACROS, "xtensa-esp-elf", "14.2.0")

    def test_reject_native_wrong_distribution(self):
        with self.assertRaisesRegex(ValueError, "pinned"):
            self.check_cc(MACROS, "xtensa-esp-elf", "15.2.0", "unreviewed gcc")

    def test_reject_sdk_pin_and_dirty_sdk(self):
        with tempfile.TemporaryDirectory() as d:
            idf = Path(d) / "idf"
            (idf / "components").mkdir(parents=True)
            with patch("native_msc_sdk.output", return_value="wrong"):
                with self.assertRaisesRegex(ValueError, "requires ESP-IDF"):
                    native_headers(idf, Path(d))
            with patch("native_msc_sdk.output", side_effect=[IDF_COMMIT, " M source.h"]):
                with self.assertRaisesRegex(ValueError, "modified"):
                    native_headers(idf, Path(d))

    def test_reject_missing_sdk(self):
        with tempfile.TemporaryDirectory() as d:
            with self.assertRaisesRegex(ValueError, "IDF_PATH"):
                native_headers(Path(d), Path(d))

    def test_framework_and_idf_are_exclusive(self):
        run = subprocess.run([sys.executable, str(ROOT / "scripts/build_usb_device_msc.py"),
                              "--tinyusb", ".", "--framework", ".", "--idf", "."],
                             text=True, capture_output=True)
        self.assertNotEqual(run.returncode, 0)
        self.assertIn("not allowed with argument", run.stderr)


@unittest.skipUnless(os.getenv("NATIVE_MSC_INTEGRATION") == "1", "set NATIVE_MSC_INTEGRATION=1")
class ActualSdk(unittest.TestCase):
    def test_phy_register_adapter(self):
        with tempfile.TemporaryDirectory() as d:
            out = Path(d)
            _, inc, _ = native_headers(None, out)
            for sanitize in (False, True):
                with self.subTest(sanitize=sanitize):
                    exe = out / ("adapter-san" if sanitize else "adapter")
                    flags = ["-fsanitize=address,undefined", "-fno-sanitize-recover=all"] if sanitize else []
                    subprocess.run(["cc", "-std=gnu11", "-O1", "-Wall", "-Wextra", "-Werror",
                                    "-Wno-unused-parameter", *flags, *["-I" + str(p) for p in inc],
                                    str(ROOT / "test/usb_device_msc/native_phy_adapter_test.c"),
                                    "-o", str(exe)], check=True)
                    subprocess.run([str(exe)], check=True, env={**os.environ, "ASAN_OPTIONS": "detect_leaks=0"})

    def test_native_target(self):
        with tempfile.TemporaryDirectory() as d:
            out = Path(d) / "msc"
            command = [sys.executable, str(ROOT / "scripts/build_usb_device_msc.py"),
                       "--tinyusb", os.environ["TINYUSB_SOURCE"], "--output", str(out)]
            validator = os.getenv("NATIVE_MSC_ELF_VALIDATOR")
            if validator:
                command += ["--validator", validator]
            subprocess.run(command, check=True, stdout=subprocess.DEVNULL)
            record = json.loads((out / "target-proof.json").read_text())
            self.assertEqual(record["imports"], ["memcpy", "memmove", "memset", "strcmp"])
            self.assertEqual(record["function_exports"], ["t5_driver_get"])
            self.assertEqual((record["elf_class"], record["elf_endianness"]), (32, "little"))
            self.assertEqual(record["loader_validator_passed"], bool(validator))
            self.assertFalse(record["release_replacement"])
            self.assertFalse(any("arduino" in p.lower() for p in record["native_dependency_sha256"]))
            self.assertEqual((out / "manifest.json").read_bytes(),
                             (ROOT / "Drivers/usb_device_msc_esp32s3/manifest.json").read_bytes())


if __name__ == "__main__":
    unittest.main()
