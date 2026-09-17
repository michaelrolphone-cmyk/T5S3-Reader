#!/usr/bin/env python3
"""Audit physical I2C ELF imports, real loader relocations and isolation.

A structural pass does not assert that a board can load or safely activate it.
The physical controller/GPIO/HAL must be in this ELF, never firmware exports.
"""
import argparse
import json
from pathlib import Path

from audit_usb_controller_elf import audit

ROOT = Path(__file__).resolve().parents[1]
DEFAULT = ROOT / 'dist/experimental/i2c-esp32s3-v2/driver.elf'
HARDWARE_IMPORT_PREFIXES = (
    'i2c_', 'gpio_', 'rtc_gpio_', 'rtc_io_', 'periph_module_',
    'usb_', 'hcd_', 'hub_', 'usbh_', 't5_',
)


def inspect(path):
    result = audit(path)
    imports = (result['missing_current_firmware_exports'] +
               result['resolved_by_current_firmware'])
    hardware = sorted(name for name in imports
                      if name.startswith(HARDWARE_IMPORT_PREFIXES))
    # Never allow the presence of a symbol in firmware's export table to
    # legitimize an embedded device-driver dependency.
    result['forbidden_peripheral_imports'] = hardware
    result['current_loader_abi_compatible'] = (
        result['current_loader_abi_compatible'] and not hardware)
    result['physical_board_validated'] = False
    result['installable'] = False
    result['status'] = ('peripheral-import-blocked' if hardware else
                        'loader-abi-blocked' if not result['current_loader_abi_compatible']
                        else 'awaiting-physical-ownership-and-board-validation')
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('elf', nargs='?', type=Path, default=DEFAULT)
    parser.add_argument('--strict', action='store_true')
    args = parser.parse_args()
    result = inspect(args.elf)
    report = args.elf.parent / 'i2c-loader-audit.json'
    report.write_text(json.dumps(result, indent=2) + '\n')
    print('Physical I2C loader audit:', result['status'], flush=True)
    print('Unresolved OS/CPU imports:',
          *result['missing_current_firmware_exports'], sep='\n  ', flush=True)
    print('Forbidden hardware implementation imports:',
          result['forbidden_peripheral_imports'], flush=True)
    print('Relocations:', result['relocations_examined'],
          'unsupported:', len(result['unsupported_relocations']), flush=True)
    print('Report:', report, flush=True)
    if (result['forbidden_peripheral_imports'] or
            not result['format_supported'] or
            result['unsupported_relocations'] or
            result['text_relocations'] or result['unexpected_exports'] or
            (args.strict and not result['current_loader_abi_compatible'])):
        parser.exit(1, 'I2C ELF fails isolation/loader compatibility.\n')


if __name__ == '__main__':
    main()
