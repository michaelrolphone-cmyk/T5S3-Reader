#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")/../.." && pwd)"
build="$(mktemp -d)"; trap 'rm -rf "$build"' EXIT
"${CC:-cc}" -std=c11 -O1 -g -Wall -Wextra -Werror \
  -fsanitize=address,undefined -fno-sanitize-recover=all \
  -I"$root/sdk/driver" "$root/test/storage_volume/sleep_extension_test.c" -o "$build/test"
ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 "$build/test"
