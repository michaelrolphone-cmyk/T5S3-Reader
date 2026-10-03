import importlib.util
from pathlib import Path
import sys
import unittest
from unittest.mock import MagicMock, patch


class TransportIdentityTest(unittest.TestCase):
    def invoke(self, actual):
        serial = MagicMock()
        esptool = MagicMock()
        chip = esptool.detect_chip.return_value
        chip.CHIP_NAME = 'ESP32-S3'
        chip.read_mac.return_value = bytes.fromhex(actual.replace(':', ''))
        spec = importlib.util.spec_from_file_location(
            'transport_under_test', Path(__file__).with_name('esptool_transport.py'))
        module = importlib.util.module_from_spec(spec)
        with patch.dict(sys.modules, serial=serial, esptool=esptool):
            spec.loader.exec_module(module)
        args = ['transport', '--expected-mac', '28:84:85:4b:57:98',
                '--port', '/dev/test', 'write-flash', '0x10000', 'test.bin']
        with patch.object(sys, 'argv', args):
            if actual != '28:84:85:4b:57:98':
                with self.assertRaisesRegex(RuntimeError, 'MAC mismatch'):
                    module.main()
                esptool.main.assert_not_called()
            else:
                module.main()
                self.assertIs(esptool.main.call_args.kwargs['esp'], chip)
                self.assertNotIn('--expected-mac', esptool.main.call_args.args[0])

    def test_wrong_chip_identity_prevents_command(self):
        self.invoke('28:84:85:4b:a1:1c')

    def test_verified_connection_is_reused_for_command(self):
        self.invoke('28:84:85:4b:57:98')


if __name__ == '__main__':
    unittest.main()
