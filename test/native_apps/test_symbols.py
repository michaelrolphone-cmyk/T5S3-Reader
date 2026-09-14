import pathlib
import sys
import unittest

repo = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(repo / 'scripts'))
from native_app_symbols import firmware_exports, validate_imports

class ImportsTest(unittest.TestCase):
    def test_file_browser_formatting_export(self):
        imports = '1: 00000000 0 NOTYPE GLOBAL DEFAULT UND snprintf'
        with self.assertRaisesRegex(ValueError, 'snprintf'):
            validate_imports(imports, {'printf'})
        self.assertEqual(validate_imports(imports, firmware_exports(repo)), {'snprintf'})

    def test_unknown_dependency_rejected(self):
        with self.assertRaisesRegex(ValueError, 'unexported_service'):
            validate_imports('2: 00000000 0 NOTYPE GLOBAL DEFAULT UND unexported_service', firmware_exports(repo))

    def test_definitions_are_not_imports(self):
        self.assertEqual(validate_imports('3: 00001000 42 FUNC GLOBAL DEFAULT 6 app_main', set()), set())

if __name__ == '__main__': unittest.main()
