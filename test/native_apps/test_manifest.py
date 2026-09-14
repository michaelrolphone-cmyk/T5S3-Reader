import json
from pathlib import Path
import sys
import tempfile
import unittest

repo = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(repo / 'scripts'))
from app_manifest import validate_manifest

class ManifestTest(unittest.TestCase):
    def test_shipped_manifests(self):
        for source in (repo / 'Apps').glob('*.c'):
            validate_manifest(source, source.with_suffix('.elf'))

    def test_reject_invalid(self):
        valid = dict(display_name='App', file_name='app.elf', min_firmware_version='1.1.5', icon='solid:f013')
        for field, value in [('file_name', '../app.elf'), ('file_name', 'other.elf'),
                             ('display_name', ''), ('display_name', 'x'*96),
                             ('min_firmware_version', '1.1.5-rc'), ('icon', 'solid:110000'),
                             ('icon', 'regular:d800'), ('icon', 'bad')]:
            with self.subTest(field=field, value=value), tempfile.TemporaryDirectory() as directory:
                source = Path(directory) / 'app.c'
                source.with_suffix('.json').write_text(json.dumps(dict(valid, **{field: value})))
                with self.assertRaises(ValueError): validate_manifest(source, Path('app.elf'))

if __name__ == '__main__': unittest.main()
