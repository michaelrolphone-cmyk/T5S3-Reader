#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
build="$(mktemp -d)"
trap 'rm -rf "$build"' EXIT
bash test/run_cpu_worker_v2_test.sh
"${CC:-cc}" -std=gnu11 -O2 -Wall -Wextra -Werror \
 -Itest/drivers/stub_cpu_cache -Isdk/driver \
 lib/elf_loader/src/esp_cpu_cache_v2.c test/drivers/cpu_cache_v2_test.c -o "$build/cache"
"$build/cache"
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -Isrc \
 test/drivers/provider_abi_profile_v2_test.cpp -o "$build/profile"
"$build/profile"
