#!/usr/bin/env bash
set -euo pipefail
repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build="$(mktemp -d)"
trap 'rm -rf "$build"' EXIT

cc -std=c11 -Wall -Wextra -Werror -fPIC -fvisibility=hidden -shared \
  -I"$repo/sdk/driver" "$repo/Drivers/program_msp/driver.c" \
  -o "$build/program-msp.so"

cc -std=c11 -Wall -Wextra -Werror -I"$repo/sdk/driver" \
  "$repo/test/drivers/program_msp_test.c" -ldl -o "$build/program-msp-test"

"$build/program-msp-test" "$build/program-msp.so"

exports="$(nm -D --defined-only "$build/program-msp.so" | awk '{print $3}')"
[[ "$exports" == "t5_driver_get" ]] || {
  echo "Unexpected MSP programmer exports: $exports" >&2
  exit 1
}
