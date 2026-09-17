#!/usr/bin/env python3
"""Audit the physical controller ELF against the *actual* native loader contract.

A successful Xtensa shared link is not an installability claim: the loader only
resolves explicitly registered symbols and implements a narrow relocation set.
Report exact missing primitives without adding any USB forwarding to firmware.
Normal mode writes a non-installable diagnostic; --strict fails on ABI blockers.
Neither mode grants electrical safety or substitutes for on-device validation.
"""
import argparse
import json
from pathlib import Path

from elftools.elf.elffile import ELFFile
from native_app_symbols import firmware_exports

ROOT = Path(__file__).resolve().parents[1]
DEFAULT_ELF = ROOT / 'dist/experimental/usb-controller-esp32s3/controller-link-experiment.elf'
# lib/elf_loader/src/esp_elf_validate.c: accepted SHT_RELA relocation types.
SUPPORTED_RELA = frozenset((2, 3, 4, 5))
USB_INTERNAL_PREFIXES = ('usb_', 'usbh_', 'hcd_', 'hub_', 'urb_')


def classify_imports(imports, exported):
    """Exact-name audit. In particular, firmware t5_* APIs are not driver ports."""
    names = set(imports)
    safe_exports = {name for name in exported if not name.startswith('t5_')}
    private_usb = sorted(name for name in names
                         if name.startswith(USB_INTERNAL_PREFIXES))
    return {
        'resolved_by_current_firmware': sorted(names & safe_exports),
        'missing_current_firmware_exports': sorted(names - safe_exports),
        'forbidden_usb_or_firmware_api_imports': sorted(set(private_usb) |
            {name for name in names if name.startswith('t5_')}),
    }


def audit(path):
    if not path.is_file():
        raise FileNotFoundError('Build physical controller first: ' + str(path))
    with path.open('rb') as stream:
        elf = ELFFile(stream)
        header = elf.header
        identity_ok = (elf.elfclass == 32 and elf.little_endian and
                       header['e_type'] == 'ET_DYN' and
                       header['e_machine'] == 'EM_XTENSA' and
                       elf.num_sections() <= 256 and path.stat().st_size <= 8 * 1024 * 1024)
        dynsym = elf.get_section_by_name('.dynsym')
        if dynsym is None:
            raise ValueError('Controller has no dynamic symbol table')
        imports = sorted({sym.name for sym in dynsym.iter_symbols()
                          if sym.name and sym['st_shndx'] == 'SHN_UNDEF' and
                          sym['st_info']['bind'] in ('STB_GLOBAL', 'STB_WEAK')})
        import_report = classify_imports(imports, firmware_exports(ROOT))
        alloc = [(section['sh_addr'], section['sh_addr'] + section['sh_size'])
                 for section in elf.iter_sections() if section['sh_flags'] & 2]
        unsupported = []
        relocation_count = 0
        for section in elf.iter_sections():
            if section['sh_type'] not in ('SHT_RELA', 'SHT_REL'):
                continue
            for rel in section.iter_relocations():
                relocation_count += 1
                address = rel['r_offset']
                kind = rel['r_info_type']
                if (section['sh_type'] != 'SHT_RELA' or kind not in SUPPORTED_RELA or
                    address % 4 or not any(lo <= address and address + 4 <= hi
                                         for lo, hi in alloc)):
                    unsupported.append({'section': section.name,
                                        'offset': hex(address), 'type': kind,
                                        'format': section['sh_type']})
        # A text relocation could modify code in PSRAM and is not approved by
        # this audit, even if a linker accepts it.
        dynamic = elf.get_section_by_name('.dynamic')
        textrel = bool(dynamic and any(tag.entry.d_tag in ('DT_TEXTREL',) or
                       (tag.entry.d_tag == 'DT_FLAGS' and tag.entry.d_val & 4)
                       for tag in dynamic.iter_tags()))
        allowed_symbols = {'t5_driver_get', '__bss_start', '_edata', '_end'}
        unexpected_exports = sorted({sym.name for sym in dynsym.iter_symbols()
                                     if sym.name and sym['st_shndx'] != 'SHN_UNDEF' and
                                     sym['st_info']['bind'] == 'STB_GLOBAL'} - allowed_symbols)
        readiness = (identity_ok and not unsupported and not textrel and
                     not unexpected_exports and
                     not import_report['missing_current_firmware_exports'] and
                     not import_report['forbidden_usb_or_firmware_api_imports'])
        return {
            'binary': path.name,
            'format_supported': identity_ok,
            'relocations_examined': relocation_count,
            'unsupported_relocations': unsupported,
            'text_relocations': textrel,
            'unexpected_exports': unexpected_exports,
            **import_report,
            'current_loader_abi_compatible': readiness,
            'physical_board_validated': False,
            'installable': False,
            'status': 'loader-abi-blocked' if not readiness else 'awaiting-board-validation',
        }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('elf', nargs='?', type=Path, default=DEFAULT_ELF)
    parser.add_argument('--strict', action='store_true',
                        help='fail CI if actual firmware ABI/relocations are incompatible')
    args = parser.parse_args()
    result = audit(args.elf)
    output = args.elf.parent / 'controller-loader-audit.json'
    output.write_text(json.dumps(result, indent=2) + '\n')
    print('Physical controller loader audit:', result['status'], flush=True)
    print('Exact currently unavailable imports:',
          *result['missing_current_firmware_exports'], sep='\n  ', flush=True)
    print('Unsupported relocations:', result['unsupported_relocations'], flush=True)
    print('Report:', output, flush=True)
    print('Not installable: board power, physical USB and safe teardown remain unverified.', flush=True)
    if args.strict and not result['current_loader_abi_compatible']:
        parser.exit(1, 'Controller not compatible with the current loader ABI.\n')


if __name__ == '__main__':
    main()
