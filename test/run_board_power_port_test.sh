#!/usr/bin/env bash
# Actual native board-power bridge against an exact-generation graph fixture.
set -euo pipefail
repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build="$(mktemp -d)"
trap 'rm -rf "$build"' EXIT
c++ -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined \
  -fno-omit-frame-pointer -DBOARD_T5S3_PRO -include cstdint \
  -I"$repo/lib/Board_T5S3" -I"$repo/lib/Board" \
  -I"$repo/test/drivers/stubs" -I"$repo/sdk/driver" -I"$repo/src" \
  "$repo/src/native/NativeBoardPowerPort.cpp" \
  "$repo/test/hal/board_power_port_test.cpp" -o "$build/test"
for scenario in transient-release failed-acquire invalid-interface \
                uncertain-configure snapshot-failure shutdown-accepted \
                shutdown-uncertain shutdown-rejected prepare-failure; do
  "$build/test" "$scenario"
done
