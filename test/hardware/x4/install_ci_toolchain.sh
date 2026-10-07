#!/usr/bin/env bash
# Official Linux CI archive; exact digest from Espressif release metadata.
set -euo pipefail
: "${RUNNER_TEMP:?Requires GitHub runner temporary directory}"
: "${GITHUB_ENV:?Requires GitHub environment file}"
archive="$RUNNER_TEMP/x4-elf-toolchain.tar.xz"
curl --fail --location --max-time 300 --retry 2 --retry-max-time 600 \
  https://github.com/espressif/crosstool-NG/releases/download/esp-14.2.0_20260121/xtensa-esp-elf-14.2.0_20260121-x86_64-linux-gnu.tar.xz \
  -o "$archive"
printf '%s  %s\n' da31f36d79d4e99f24e55a90a71e65d5694714f16199960bf7885724b706a48c "$archive" | sha256sum -c -
mkdir -p "$RUNNER_TEMP/x4-elf-toolchain"
tar -xJf "$archive" -C "$RUNNER_TEMP/x4-elf-toolchain"
test -x "$RUNNER_TEMP/x4-elf-toolchain/xtensa-esp-elf/bin/xtensa-esp32s3-elf-gcc"
echo "X4_ELF_TOOLCHAIN=$RUNNER_TEMP/x4-elf-toolchain/xtensa-esp-elf/bin" >> "$GITHUB_ENV"
