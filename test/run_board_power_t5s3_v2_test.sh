#!/usr/bin/env bash
set -euo pipefail
repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build="$(mktemp -d)"
trap 'rm -rf "$build"' EXIT
binary="$build/test"
cc -std=c11 -Wall -Wextra -Werror -Dt5_driver_get=t5_profile_get \
  -I"$repo/sdk/driver" -c "$repo/Drivers/t5s3_usb_power_profile/driver.c" \
  -o "$build/profile.o"
cc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined \
  -fno-omit-frame-pointer -I"$repo/sdk/driver" \
  "$repo/Drivers/bq25896/driver.c" \
  "$repo/test/drivers/board_power_t5s3_v2_test.c" "$build/profile.o" -o "$binary"
"$binary"
