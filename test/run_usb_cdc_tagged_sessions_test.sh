#!/usr/bin/env bash
set -euo pipefail
repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build="$(mktemp -d)"
trap 'rm -rf "$build"' EXIT
flags=(-std=c11 -Wall -Wextra -Werror)
if [[ "${SANITIZE:-0}" == 1 ]]; then
  flags+=(-fsanitize=address,undefined -fno-omit-frame-pointer -g)
fi
cc "${flags[@]}" -fPIC -fvisibility=hidden -shared -I"$repo/sdk/driver" \
  "$repo/Drivers/usb_cdc_v2/driver.c" -o "$build/cdc.so"
cc "${flags[@]}" -I"$repo/sdk/driver" \
  "$repo/test/drivers/usb_cdc_tagged_sessions_test.c" -ldl -o "$build/tagged"
for scenario in normal legacy-host bad-suffix invalid inventory configure-short claim-fail \
  claim-retained claim-malformed partial-open partial-retained deadline-clean deadline-retained \
  overrun io-presence call-deadline call-overrun close-deadline close-retained queue-retained disconnect; do
  "$build/tagged" "$build/cdc.so" "$scenario"
done
for fixture in usb_cdc_v2_test usb_cdc_v2_descriptors_test usb_stlink_vcp_test; do
  cc "${flags[@]}" -I"$repo/sdk/driver" "$repo/test/drivers/$fixture.c" -ldl -o "$build/$fixture"
  "$build/$fixture" "$build/cdc.so"
done
[[ "$(nm -D --defined-only "$build/cdc.so" | awk '{print $3}')" == t5_driver_get ]]
