#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
exe="$(mktemp)"
trap 'rm -f "$exe"' EXIT
"${CC:-cc}" -std=gnu11 -O2 -fno-builtin -fno-stack-protector \
  -Wall -Wextra -Werror \
  -Itest/drivers/stub_os_cpu -Ilib/elf_loader/include \
  lib/elf_loader/src/esp_privileged_os_cpu.c \
  test/drivers/privileged_os_cpu_v1_test.c -o "$exe"
"$exe"
"${CC:-cc}" -std=gnu11 -O2 -fno-builtin -fno-stack-protector \
  -Wall -Wextra -Werror \
  -Itest/drivers/stub_os_cpu -Ilib/elf_loader/include \
  lib/elf_loader/src/esp_privileged_imports.c \
  test/drivers/privileged_imports_v1_test.c -o "$exe"
"$exe"
"${CC:-cc}" -std=gnu11 -O2 -fno-builtin -fno-stack-protector \
  -Wall -Wextra -Werror \
  -Itest/drivers/stub_os_cpu -Ilib/elf_loader/include \
  lib/elf_loader/src/esp_privileged_imports.c \
  lib/elf_loader/src/esp_privileged_manifest_imports.c \
  test/drivers/privileged_manifest_imports_v1_test.c -o "$exe"
"$exe"
