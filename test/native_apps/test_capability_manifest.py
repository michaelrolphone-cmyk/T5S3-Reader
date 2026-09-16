import json
import pathlib
import sys
import tempfile
import unittest

repo = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(repo / 'scripts'))
from app_manifest import validate_manifest


class CapabilityManifestTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.source = pathlib.Path(self.temp.name) / 'example.c'
        self.path = self.source.with_suffix('.json')
        self.output = pathlib.Path(self.temp.name) / 'example.elf'
        self.data = dict(display_name='Example', file_name='example.elf',
                         version='1.0.0', min_firmware_version='1.2.9', icon='solid:f013')

    def check(self, requires=None, optional=None):
        data = dict(self.data)
        if requires is not None:
            data['requires'] = requires
        if optional is not None:
            data['optional'] = optional
        self.path.write_text(json.dumps(data), encoding='utf-8')
        return validate_manifest(self.source, self.output)

    def test_legacy_manifest_accepted(self):
        self.assertEqual(self.check(), self.path)

    def test_versioned_required_and_optional(self):
        self.assertEqual(self.check([{'capability': 'location.position', 'api': '>=1'}],
                                    [{'capability': 'network.internet', 'api': '>=2'}]), self.path)
        self.assertEqual(self.check([]), self.path)

    def test_bad_requirement_shapes(self):
        for value in ('location.position', 12, [1], [{'capability': 'location.position'}],
                      [{'capability': 'location.position', 'api': 1}],
                      [{'capability': 'location.position', 'api': '>=1', 'mode': 'exclusive'}]):
            with self.subTest(value=value), self.assertRaises(ValueError):
                self.check(value)

    def test_invalid_ids_and_apis(self):
        for name in ('Location.Position', 'bad/path', '', 'x' * 40):
            with self.subTest(name=name), self.assertRaises(ValueError):
                self.check([{'capability': name, 'api': '>=1'}])
        for version in ('1', '>=0', '>=01', '>=65536', '>=1.2', ''):
            with self.subTest(version=version), self.assertRaises(ValueError):
                self.check([{'capability': 'serial.port', 'api': version}])

    def test_duplicate_and_overflow(self):
        entry = {'capability': 'serial.port', 'api': '>=1'}
        with self.assertRaises(ValueError):
            self.check([entry, entry])
        with self.assertRaises(ValueError):
            self.check([entry], [entry])
        with self.assertRaises(ValueError):
            self.check([{'capability': f'capability.{i}', 'api': '>=1'} for i in range(7)])

    def test_runtime_size_limit(self):
        self.data['notes'] = 'x' * 2200
        with self.assertRaisesRegex(ValueError, 'runtime limits'):
            self.check()


if __name__ == '__main__':
    unittest.main()
