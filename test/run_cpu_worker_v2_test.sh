#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
build="$(mktemp -d)"
exe="$build/test"
trap 'rm -rf "$build"' EXIT
"${CC:-cc}" -std=gnu11 -shared -fPIC test/drivers/cpu_worker_v2_fixture.c -o "$build/provider.so"
"${CC:-cc}" -std=gnu11 -O2 -Wall -Wextra -Werror -pthread \
 -Itest/drivers/stub_cpu_worker -Isdk/driver \
 lib/elf_loader/src/esp_cpu_worker_v2.c test/drivers/cpu_worker_v2_test.c -ldl -o "$exe"
"$exe" "$build/provider.so"
