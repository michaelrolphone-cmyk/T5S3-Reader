#!/usr/bin/env bash
set -euo pipefail
repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build="$(mktemp -d)"
trap 'rm -rf "$build"' EXIT
binary="$build/test"
cc -std=c11 -Wall -Wextra -Werror -Dt5_driver_get=t5_profile_get \
  -I"$repo/sdk/driver" -c "$repo/Drivers/t5s3_usb_power_profile/driver.c" \
  -o "$build/profile.o"
for test_case in board_power_t5s3_v2 board_power_snapshot board_power_charge_profile board_power_shutdown; do
  cc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined \
    -fno-omit-frame-pointer -I"$repo/sdk/driver" \
    "$repo/Drivers/bq25896/driver.c" \
    "$repo/test/drivers/${test_case}_test.c" "$build/profile.o" -o "$binary"
  "$binary"
  if [[ "$test_case" == board_power_shutdown ]]; then
    "$binary" batfet-nack
  fi
done
python3 "$repo/test/resources/board_power_cutover_source_test.py"
