#!/usr/bin/env python3
"""Audit the transitional i2c.bus ELF and its single private firmware import.

No IDF I2C, GPIO, HAL or ISR implementation belongs in this adapter. The
public provider interface stays unchanged; physical I2C0 is serialized by
firmware until the full I2C ownership cutover. An ordinary application cannot
import the dedicated transport, and other providers must depend on i2c.bus.
"""
import argparse
import json
from pathlib import Path

from audit_usb_controller_elf import audit
from generate_privileged_imports_v1 import extract_imports
from verify_provider_relocation_map import audit_loader_map

ROOT = Path(__file__).resolve().parents[1]
DEFAULT = ROOT / 'dist/experimental/i2c-esp32s3-v2/driver.elf'
BRIDGE = 'risc_fw_i2c_transact_v1'
HARDWARE_IMPORT_PREFIXES = (
    'i2c_', 'gpio_', 'rtc_gpio_', 'rtc_io_', 'periph_module_',
    'usb_', 'hcd_', 'hub_', 'usbh_', 't5_',
)


def inspect(path):
    result = audit(path)
    mapping = audit_loader_map(path)
    result.update(mapping)
    imports = extract_imports(path)
    # The dedicated private transport is intentionally absent from the
    # GENERIC privileged OS/CPU and libc inventories. It is explicitly
    # accepted only by the firmware's private preflight and exact-ID admission.
    bridge_present = BRIDGE in imports
    result['missing_privileged_os_cpu_v1'] = [name for name in
        result['missing_privileged_os_cpu_v1'] if name != BRIDGE]
    result['rejected_by_private_loader_import_preflight'] = [name for name in
        result['rejected_by_private_loader_import_preflight'] if name != BRIDGE]
    hardware = sorted(name for name in imports
                      if name.startswith(HARDWARE_IMPORT_PREFIXES))
    result['forbidden_peripheral_imports'] = hardware
    result['firmware_i2c_private_import'] = bridge_present
    result['i2c_mmio_relocations'] = mapping['absolute_peripheral_relocations']
    layout_ok = not (mapping['unmapped_relocations'] or
                     mapping['unmapped_relative_values'] or
                     mapping['unmapped_executable_sections'])
    isolated = (bridge_present and not hardware and layout_ok and
                not mapping['absolute_peripheral_relocations'] and
                result['format_supported'] and not result['unsupported_relocations'] and
                not result['text_relocations'] and not result['unexpected_exports'] and
                not result['forbidden_usb_or_firmware_api_imports'] and
                not result['missing_privileged_os_cpu_v1'] and
                not result['rejected_by_private_loader_import_preflight'])
    result['current_loader_abi_compatible'] = False  # Not an ordinary-app import.
    result['privileged_os_cpu_v1_import_compatible'] = isolated
    result['privileged_loader_admission_integrated'] = isolated
    result['physical_board_validated'] = False
    result['installable'] = False
    result['status'] = ('firmware-i2c-private-adapter-verified' if isolated else
                        'firmware-i2c-adapter-audit-blocked')
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('elf', nargs='?', type=Path, default=DEFAULT)
    parser.add_argument('--strict', action='store_true')
    args = parser.parse_args()
    result = inspect(args.elf)
    report = args.elf.parent / 'i2c-loader-audit.json'
    report.write_text(json.dumps(result, indent=2) + '\n')
    print('Firmware-backed I2C ELF audit:', result['status'], flush=True)
    print('Only adapter may import:', BRIDGE, flush=True)
    print('Other physical hardware imports:', result['forbidden_peripheral_imports'], flush=True)
    print('Absolute MMIO references:', result['i2c_mmio_relocations'], flush=True)
    print('Relocations:', result['relocations_examined'],
          'unsupported:', len(result['unsupported_relocations']),
          'unmapped sites:', len(result['unmapped_relocations']),
          'invalid values:', len(result['unmapped_relative_values']), flush=True)
    print('Report:', report, flush=True)
    if (args.strict and not result['privileged_os_cpu_v1_import_compatible']) or (
            result['forbidden_peripheral_imports'] or
            not result['format_supported'] or
            result['unsupported_relocations'] or
            result['unmapped_relocations'] or
            result['unmapped_relative_values'] or
            result['unmapped_executable_sections'] or
            result['text_relocations'] or result['unexpected_exports'] or
            result['i2c_mmio_relocations'] or not result['firmware_i2c_private_import']):
        parser.exit(1, 'I2C ELF fails private bridge or runtime loader isolation.\n')


if __name__ == '__main__':
    main()
