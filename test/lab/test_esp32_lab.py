"""Durable no-wrong-target/no-overlap guards; never opens a serial device."""
import importlib.util
import struct
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

spec = importlib.util.spec_from_file_location('lab', Path(__file__).resolve().parents[2] / 'scripts/esp32_lab.py')
lab = importlib.util.module_from_spec(spec)
spec.loader.exec_module(lab)


def table(entries):
    data = b''.join(struct.pack('<HBBII16sI', 0x50aa, *p, b'test', 0) for p in entries)
    return data + b'\xff' * (4096 - len(data))


class GuardTests(unittest.TestCase):
    def test_mac_os_port_aliases_share_lock(self):
        with tempfile.TemporaryDirectory() as directory:
            with lab.DeviceLocks(directory, '/dev/cu.lab-test'):
                with self.assertRaisesRegex(RuntimeError, 'Device busy'):
                    with lab.DeviceLocks(directory, '/dev/tty.lab-test'):
                        self.fail('Alias acquired the same serial interface')

    def test_serial_close_failure_releases_locks(self):
        class BadPort:
            def close(self): raise OSError('close failed')
        with tempfile.TemporaryDirectory() as directory:
            locks = lab.DeviceLocks(directory, '/dev/cu.lab-test')
            locks.__enter__()
            with self.assertRaisesRegex(OSError, 'close failed'):
                lab.close_device(BadPort(), locks)
            with lab.DeviceLocks(directory, '/dev/cu.lab-test'):
                pass

    def test_other_process_is_rejected_and_lock_is_reusable(self):
        with tempfile.TemporaryDirectory() as directory:
            script = ("import importlib.util; from pathlib import Path; "
                f"s=importlib.util.spec_from_file_location('lab',{str(spec.origin)!r}); "
                "m=importlib.util.module_from_spec(s); s.loader.exec_module(m); "
                f"m.DeviceLocks({directory!r},'/dev/cu.lab-test','28:84:85:4b:a1:1c').__enter__()")
            with lab.DeviceLocks(directory, '/dev/cu.lab-test', '28:84:85:4b:a1:1c'):
                result = subprocess.run([sys.executable, '-B', '-c', script], capture_output=True)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn(b'Device busy', result.stderr)
            self.assertEqual(subprocess.run([sys.executable, '-B', '-c', script], capture_output=True).returncode, 0)

    def test_same_mac_cannot_be_locked_under_another_port(self):
        with tempfile.TemporaryDirectory() as directory:
            with lab.DeviceLocks(directory, '/dev/cu.first', '28:84:85:4b:a1:1c'):
                with self.assertRaisesRegex(RuntimeError, 'Device busy'):
                    with lab.DeviceLocks(directory, '/dev/cu.second', '28:84:85:4b:a1:1c'):
                        self.fail('Alias acquired the same board')

    def test_existing_factory_layout_accepts_fitting_image(self):
        self.assertEqual(lab.factory_region(table([(1, 2, 0x9000, 0x6000),
            (1, 1, 0xf000, 0x1000), (0, 0, 0x10000, 0x1f0000)]), 278240), 0x10000)

    def test_erase_rounding_cannot_touch_next_partition(self):
        with self.assertRaisesRegex(RuntimeError, 'erase range'):
            lab.factory_region(table([(0, 0, 0x10000, 5000), (1, 2, 0x10000 + 5000, 4096)]), 4999)

    def test_ota_layout_is_not_guessed(self):
        with self.assertRaisesRegex(RuntimeError, 'single-factory'):
            lab.factory_region(table([(0, 0x10, 0x10000, 0x100000)]), 200000)

    def test_overlap_cannot_hide_nvs_inside_app(self):
        with self.assertRaisesRegex(RuntimeError, 'Overlapping'):
            lab.factory_region(table([(0, 0, 0x10000, 0x100000), (1, 2, 0x20000, 4096)]), 200000)

    def test_mac_mismatch_stops(self):
        class Esp:
            CHIP_NAME = 'ESP32-S3'
            def read_mac(self): return bytes.fromhex('2884854b5711')
        with self.assertRaisesRegex(RuntimeError, 'MAC mismatch'):
            lab.verify_target(Esp(), '28:84:85:4b:a1:1c')

    def test_wrong_chip_stops_even_with_expected_mac(self):
        class Esp:
            CHIP_NAME = 'ESP32'
            def read_mac(self): return bytes.fromhex('2884854ba11c')
        with self.assertRaisesRegex(RuntimeError, 'Unexpected chip'):
            lab.verify_target(Esp(), '28:84:85:4b:a1:1c')


if __name__ == '__main__': unittest.main()
