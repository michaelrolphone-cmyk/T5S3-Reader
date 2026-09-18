#!/usr/bin/env python3
"""Integrate physical-driver package creation with the opt-in release workflow.

This only edits the PR source. It does NOT run a release, tag, flash, publish or
modify master. The normal authorized release flow later builds these ELFs and
uploads canonical assets alongside legacy drivers.
"""
from pathlib import Path

PATH = Path(__file__).resolve().parents[1] / '.github/workflows/release.yml'
STEP = '''      - name: Build real hardware-owning USB providers and canonical packages
        run: |
          python -m pip install pyelftools
          pio pkg install -e t5s3-pro
          python scripts/probe_usb_controller_esp32s3.py --link-experiment
          python scripts/audit_usb_controller_elf.py --strict
          python scripts/probe_i2c_esp32s3_v2.py
          python scripts/audit_i2c_esp32s3_elf.py --strict
          python scripts/build_platform_clock_v1.py
          python scripts/build_board_power_t5s3_v2.py
          python scripts/build_usb_host_v2.py
          python scripts/build_usb_cdc_v2.py
          python scripts/build_usb_cp210x_v2.py
          python scripts/build_installed_usb_stack.py
          python test/drivers/installed_usb_stack_package_test.py
          python scripts/export_canonical_driver_release.py

'''


def replace_once(source: str, old: str, new: str) -> str:
    if source.count(old) != 1:
        raise ValueError(f'Unexpected release workflow marker: {old!r}')
    return source.replace(old, new, 1)


def main() -> None:
    before = PATH.read_text(encoding='utf-8')
    if 'Build real hardware-owning USB providers and canonical packages' in before:
        print('Canonical physical-driver release integration already applied')
        return
    source = replace_once(before,
        '      - name: Verify baked RiscRTE version string\n',
        STEP + '      - name: Verify baked RiscRTE version string\n')
    source = replace_once(source, '          ASSETS=(\n',
        '''          mapfile -d '' CANONICAL_DRIVER_ASSETS < <(
            find dist/release-packages -maxdepth 1 -type f -print0 | sort -z
          )

          ASSETS=(
''')
    source = replace_once(source, '            "${APP_ELFS[@]}"\n',
        '            "${APP_ELFS[@]}"\n            "${CANONICAL_DRIVER_ASSETS[@]}"\n')
    PATH.write_text(source, encoding='utf-8')
    print('Integrated canonical driver build and uniquely named assets into future authorized releases')


if __name__ == '__main__':
    main()
