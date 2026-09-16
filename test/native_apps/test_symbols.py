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

    def test_stream_api_export(self):
        imports = '1: 00000000 0 NOTYPE GLOBAL DEFAULT UND t5_stream_get_api'
        self.assertEqual(validate_imports(imports, firmware_exports(repo)), {'t5_stream_get_api'})

    def test_serial_port_api_export(self):
        imports = '1: 00000000 0 NOTYPE GLOBAL DEFAULT UND t5_serial_port_get_api'
        self.assertEqual(validate_imports(imports, firmware_exports(repo)), {'t5_serial_port_get_api'})

    def test_device_observation_api_export(self):
        imports = '1: 00000000 0 NOTYPE GLOBAL DEFAULT UND t5_device_get_api'
        self.assertEqual(validate_imports(imports, firmware_exports(repo)), {'t5_device_get_api'})

    def test_programmer_api_export(self):
        imports = '1: 00000000 0 NOTYPE GLOBAL DEFAULT UND t5_program_esp_rom_get_api'
        self.assertEqual(validate_imports(imports, firmware_exports(repo)), {'t5_program_esp_rom_get_api'})

    def test_flasher_and_programmer_boundaries(self):
        flasher = (repo / 'Apps/esp_rom_flasher.c').read_text()
        provider = (repo / 'src/native/NativeEspRomBridge.cpp').read_text()
        self.assertIn('T5ProgramEspRomApi.h', flasher)
        self.assertIn('streams->open_file', flasher)
        self.assertIn('programmer->program', flasher)
        self.assertNotIn('T5UsbApi.h', flasher)
        self.assertNotIn('t5_usb_get_api', flasher)
        self.assertNotIn('usb_host_', flasher)
        self.assertNotIn('esp_rom_md5', flasher)
        self.assertIn('t5_serial_port_get_api', provider)
        self.assertIn('t5_stream_get_api', provider)
        self.assertNotIn('t5_usb_get_api', provider)
        self.assertNotIn('usb_host_', provider)

    def test_unknown_dependency_rejected(self):
        with self.assertRaisesRegex(ValueError, 'unexported_service'):
            validate_imports('2: 00000000 0 NOTYPE GLOBAL DEFAULT UND unexported_service', firmware_exports(repo))

    def test_definitions_are_not_imports(self):
        self.assertEqual(validate_imports('3: 00001000 42 FUNC GLOBAL DEFAULT 6 app_main', set()), set())

if __name__ == '__main__': unittest.main()
