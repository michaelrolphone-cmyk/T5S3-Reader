#!/usr/bin/env bash
set -euo pipefail
repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
binary="$(mktemp)"
trap 'rm -f "$binary"' EXIT
for test_case in board_power_t5s3_v2 board_power_snapshot; do
  cc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined \
    -fno-omit-frame-pointer -I"$repo/sdk/driver" \
    "$repo/Drivers/board_power_t5s3_v2/driver.c" \
    "$repo/test/drivers/${test_case}_test.c" -o "$binary"
  "$binary"
done
