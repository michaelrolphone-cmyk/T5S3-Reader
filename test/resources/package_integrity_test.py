import hashlib
import tempfile
import unittest
from pathlib import Path
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'scripts'))
from package_integrity import stamp_app_manifest

class TestReleaseIntegrity(unittest.TestCase):
    def test_hash_size_bind_actual_bytes(self):
        with tempfile.TemporaryDirectory() as path:
            elf = Path(path) / 'app.elf'
            elf.write_bytes(b'\x7fELF' + b'X' * 128)
            original = {'file_name': 'app.elf', 'version': '1.0.0'}
            result = stamp_app_manifest(original, elf)
            self.assertEqual(result['size_bytes'], 132)
            self.assertEqual(result['sha256'], hashlib.sha256(elf.read_bytes()).hexdigest())
            self.assertEqual(original, {'file_name': 'app.elf', 'version': '1.0.0'})
            self.assertEqual(stamp_app_manifest(result, elf), result)
            elf.write_bytes(b'\x7fELF' + b'Y' * 128)
            with self.assertRaises(ValueError): stamp_app_manifest(result, elf)
    def test_reject_mismatch_and_oversize(self):
        with tempfile.TemporaryDirectory() as path:
            elf = Path(path) / 'app.elf'; elf.write_bytes(b'A' * 64)
            for record in ({'file_name':'other.elf'}, {'file_name':'app.elf','size_bytes':True},
                           {'file_name':'app.elf','size_bytes':65},
                           {'file_name':'app.elf','sha256':'0'*64}):
                with self.assertRaises(ValueError): stamp_app_manifest(record, elf)
            with self.assertRaises(ValueError):
                stamp_app_manifest({'file_name':'app.elf','note':'x'*2048}, elf)

if __name__ == '__main__': unittest.main()
