#!/usr/bin/env bash
set -euo pipefail
repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build="$(mktemp -d)"
trap 'rm -rf "$build"' EXIT
flags=(-std=c11 -Wall -Wextra -Werror)
if [[ "${SANITIZE:-0}" == 1 ]]; then flags+=(-fsanitize=address,undefined -fno-omit-frame-pointer -g); fi
vendor=0
for cls in ch34x_v2 cp210x_v2 ftdi; do
  vendor=$((vendor+1))
  cc "${flags[@]}" -fPIC -fvisibility=hidden -shared -I"$repo/sdk/driver" \
    "$repo/Drivers/usb_$cls/driver.c" -o "$build/class.so"
  cc "${flags[@]}" -DVENDOR="$vendor" -I"$repo/sdk/driver" \
    "$repo/test/drivers/usb_vendor_tagged_sessions_test.c" -ldl -o "$build/tagged"
  for scenario in normal legacy-host bad-suffix invalid inventory configure-short claim-fail \
    claim-retained claim-malformed partial-open partial-retained deadline-clean deadline-retained \
    overrun io-presence call-deadline call-overrun close-deadline close-retained queue-retained disconnect \
    descriptor-short framing copied-open copied-call control-retained control-call-retained write-retained finish-refused publish-malformed publish-duplicate io-retained io-overrun; do
    "$build/tagged" "$build/class.so" "$scenario"
  done
  case "$vendor" in 1) count=4; extra=(old-chip);; 2) count=3; extra=(cleanup-control cleanup-unknown);; 3) count=7; extra=(highspeed ftx status-1 status-2 status-3 cleanup-control cleanup-unknown);; esac
  for ((i=1;i<=count;++i)); do "$build/tagged" "$build/class.so" "short-$i"; done
  for scenario in "${extra[@]}"; do "$build/tagged" "$build/class.so" "$scenario"; done
  cc "${flags[@]}" -I"$repo/sdk/driver" "$repo/test/drivers/usb_${cls}_test.c" -ldl -o "$build/legacy"
  "$build/legacy" "$build/class.so"
  [[ "$(nm -D --defined-only "$build/class.so" | awk '{print $3}')" == t5_driver_get ]]
done
