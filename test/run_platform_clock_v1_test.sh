#!/usr/bin/env bash
set -euo pipefail
repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
binary="$(mktemp)"
trap 'rm -f "$binary"' EXIT
cc -std=c11 -D_DEFAULT_SOURCE -Wall -Wextra -Werror \
  -fsanitize=address,undefined -fno-omit-frame-pointer \
  -I"$repo/sdk/driver" \
  "$repo/Drivers/platform_clock_v1/driver.c" \
  "$repo/test/drivers/platform_clock_v1_test.c" -o "$binary"
"$binary"
