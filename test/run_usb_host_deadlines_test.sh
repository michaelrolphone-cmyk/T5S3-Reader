#!/usr/bin/env bash
set -euo pipefail
repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build="$(mktemp -d)"
trap 'rm -rf "$build"' EXIT
flags=(-std=c11 -Wall -Wextra -Werror -g -I"$repo/sdk/driver")
link=()
if [[ "${SANITIZE:-0}" == 1 ]]; then
  flags+=(-fsanitize=address,undefined -fno-omit-frame-pointer)
  link+=(-no-pie)
fi
cc "${flags[@]}" -fPIC -fvisibility=hidden -shared \
  "$repo/Drivers/usb_host_v2/driver.c" -o "$build/host.so"
cc "${flags[@]}" "${link[@]}" "$repo/test/drivers/usb_host_deadlines_test.c" -ldl -o "$build/test"
for scenario in gate events inventory_fault valid partial_setup partial_claim duplicate_claim unknown_claim \
    claim_overrun timeout cancel stale release_failure io_retained io_timeout io_overrun \
    event_overrun clock_reversal scope invalid; do
  "$build/test" "$build/host.so" "$scenario"
done
