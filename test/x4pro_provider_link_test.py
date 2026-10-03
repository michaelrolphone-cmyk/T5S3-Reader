#!/usr/bin/env python3
"""Exercise loader-compatible pointers and the clock's actual firmware ABI guard."""
import io
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest
from elftools.elf.elffile import ELFFile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'scripts'))
from validate_xtensa_relative_targets import validate


class ProviderLinkTest(unittest.TestCase):
    def test_relative_target_rejection(self):
        source = ROOT / 'dist/experimental/x4pro-panel/driver.elf'
        self.assertGreater(validate(source), 0)
        payload = bytearray(source.read_bytes())
        elf = ELFFile(io.BytesIO(payload))
        location = None
        for section in elf.iter_sections():
            if section['sh_type'] != 'SHT_RELA':
                continue
            for relocation in section.iter_relocations():
                if relocation['r_info_type'] != 5:
                    continue
                offset = relocation['r_offset']
                for target in elf.iter_sections():
                    if target['sh_type'] == 'SHT_PROGBITS' and target['sh_addr'] <= offset < target['sh_addr'] + target['sh_size']:
                        location = target['sh_offset'] + offset - target['sh_addr']
                        break
                if location is not None:
                    break
            if location is not None:
                break
        self.assertIsNotNone(location)
        struct.pack_into('<I', payload, location, 0xfffffff0)
        with tempfile.TemporaryDirectory() as temporary:
            corrupt = Path(temporary) / 'panel.elf'
            corrupt.write_bytes(payload)
            with self.assertRaisesRegex(ValueError, 'Unmappable relative target'):
                validate(corrupt)

    def test_clock_rejects_toolchain_default_time64(self):
        cc = os.environ.get('NATIVE_DRIVER_CC')
        self.assertTrue(cc, 'Run with the pinned provider NATIVE_DRIVER_CC')
        version = subprocess.check_output([cc, '--version'], text=True)
        self.assertIn('esp-14.2.0_20260121', version)
        command = [cc, '-std=c11', '-D_DEFAULT_SOURCE', '-fsyntax-only',
                   '-I'+str(ROOT/'sdk/driver'), str(ROOT/'Drivers/platform_clock_v1/driver.c')]
        wrong = subprocess.run(command, capture_output=True, text=True)
        self.assertNotEqual(wrong.returncode, 0)
        self.assertIn('platform.clock requires firmware time32 ABI', wrong.stderr)
        subprocess.run([*command, '-D_USE_LONG_TIME_T'], check=True)


if __name__ == '__main__':
    unittest.main()
