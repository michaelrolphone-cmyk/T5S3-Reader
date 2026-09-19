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
  lib/elf_loader/src/esp_privileged_os_cpu.c \
  test/drivers/privileged_provider_printf_v1_test.c -o "$exe"
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
# Compile the REAL private relocation entry with mocked backend, never run ELF.
"${CC:-cc}" -std=gnu11 -O2 -fno-builtin -fno-stack-protector \
  -Wall -Wextra -Werror -DRISC_TEST_FULL_ELF_TYPES \
  -Itest/drivers/stub_privileged_loader \
  -Itest/drivers/stub_os_cpu -Ilib/elf_loader/include \
  -include lib/elf_loader/include/private/elf_types.h \
  lib/elf_loader/src/esp_privileged_imports.c \
  lib/elf_loader/src/esp_privileged_manifest_imports.c \
  lib/elf_loader/src/esp_privileged_elf.c \
  test/drivers/privileged_elf_manifest_gate_v1_test.c -o "$exe"
"$exe"
# Exercise the actual production resolver with malicious custom hooks.
python3 test/drivers/privileged_resolver_v1_test.py
# Exercise the actual production public relocation entry with nested app loads.
python3 test/drivers/privileged_relocation_entry_v1_test.py
