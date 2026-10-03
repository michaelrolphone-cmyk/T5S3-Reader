#!/usr/bin/env bash
# Build only; never opens devices. Firmware retains its PlatformIO compiler.
set -euo pipefail
repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
cd "$repo"
: "${X4_ELF_TOOLCHAIN:?Set X4_ELF_TOOLCHAIN to the pinned Espressif compiler bin directory}"
export NATIVE_DRIVER_CC="$X4_ELF_TOOLCHAIN/xtensa-esp32s3-elf-gcc"
"$NATIVE_DRIVER_CC" --version | head -1 | grep -F 'esp-14.2.0_20260121'
# Reject historical utility branches until corrected source is consolidated.
# Never regenerate embedded ELF arrays or an internal flash driver image.
test -f src/platform/SdPackageBoot.cpp && test -f scripts/stage_x4pro_packages.py || {
  echo 'Corrected ordinary-SD production source absent; no X4 candidate generated.' >&2
  exit 1
}
export RISCRTE_X4_LINK_PROFILE=esp14-no-relax
python scripts/build_platform_clock_v1.py
python scripts/build_x4pro_drivers.py
python scripts/stage_x4pro_packages.py
python test/x4pro_boot_isolation_test.py
python test/x4pro_import_match_test.py
pio run -e xteink-x4-pro
