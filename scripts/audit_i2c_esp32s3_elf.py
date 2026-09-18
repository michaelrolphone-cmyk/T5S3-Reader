#!/usr/bin/env python3
"""Audit physical I2C ELF imports, real loader relocations and isolation.

A structural/scoped import pass is NOT hardware acceptance. The final ELF's
RELATIVE pointer values must be runtime-mappable or exact SHN_ABS MMIO symbols.
Physical controller/GPIO/HAL implementations must remain within the ELF.
"""
import argparse
import json
from pathlib import Path

from audit_usb_controller_elf import audit
from verify_provider_relocation_map import audit_loader_map

ROOT = Path(__file__).resolve().parents[1]
DEFAULT = ROOT / 'dist/experimental/i2c-esp32s3-v2/driver.elf'
HARDWARE_IMPORT_PREFIXES = (
    'i2c_', 'gpio_', 'rtc_gpio_', 'rtc_io_', 'periph_module_',
    'usb_', 'hcd_', 'hub_', 'usbh_', 't5_',
)


def inspect(path):
    result = audit(path)
    mapping = audit_loader_map(path)
    result.update(mapping)
    imports = (result['missing_current_firmware_exports'] +
               result['resolved_by_current_firmware'])
    hardware = sorted(name for name in imports
                      if name.startswith(HARDWARE_IMPORT_PREFIXES))
    result['forbidden_peripheral_imports'] = hardware
    layout_ok = not (mapping['unmapped_relocations'] or
                     mapping['unmapped_relative_values'] or
                     mapping['unmapped_executable_sections'])
    result['current_loader_abi_compatible'] = (
        result['current_loader_abi_compatible'] and not hardware and layout_ok)
    result['privileged_os_cpu_v1_import_compatible'] = (
        result['privileged_os_cpu_v1_import_compatible'] and not hardware and layout_ok)
    result['privileged_loader_admission_integrated'] = False
    result['physical_board_validated'] = False
    result['installable'] = False
    result['status'] = ('peripheral-import-blocked' if hardware else
                        'loader-section-map-blocked' if not layout_ok else
                        'loader-abi-blocked' if not result['privileged_os_cpu_v1_import_compatible']
                        else 'privileged-imports-covered-awaiting-signed-admission')
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
    print('Unresolved ordinary-app OS/CPU imports:',
          *result['missing_current_firmware_exports'], sep='\n  ', flush=True)
    print('Missing scoped privileged OS/CPU imports:',
          *result['missing_privileged_os_cpu_v1'], sep='\n  ', flush=True)
    print('Rejected by runtime private import preflight:',
          *result['rejected_by_private_loader_import_preflight'], sep='\n  ', flush=True)
    print('Forbidden hardware implementation imports:',
          result['forbidden_peripheral_imports'], flush=True)
    print('Relocations:', result['relocations_examined'],
          'unsupported:', len(result['unsupported_relocations']),
          'unmapped sites:', len(result['unmapped_relocations']),
          'invalid values:', len(result['unmapped_relative_values']),
          'validated ABS MMIO:', len(result['absolute_peripheral_relocations']), flush=True)
    print('Unmapped executable sections:', result['unmapped_executable_sections'], flush=True)
    print('Report:', report, flush=True)
    if (result['forbidden_peripheral_imports'] or
            not result['format_supported'] or
            result['unsupported_relocations'] or
            result['unmapped_relocations'] or
            result['unmapped_relative_values'] or
            result['unmapped_executable_sections'] or
            result['text_relocations'] or result['unexpected_exports'] or
            (args.strict and not result['privileged_os_cpu_v1_import_compatible'])):
        parser.exit(1, 'I2C ELF fails isolation or actual runtime loader mapping.\n')


if __name__ == '__main__':
    main()
