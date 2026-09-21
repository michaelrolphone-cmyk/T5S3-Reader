#!/usr/bin/env bash
set -euo pipefail
repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build="$(mktemp -d)"
trap 'rm -rf "$build"' EXIT
cc -std=c11 -Wall -Wextra -Werror -fPIC -fvisibility=hidden -shared \
  -I"$repo/sdk/driver" "$repo/Drivers/usb_host_v2/driver.c" \
  -o "$build/usb-host-v2.so"
cc -std=c11 -Wall -Wextra -Werror -I"$repo/sdk/driver" \
  "$repo/test/drivers/usb_host_v2_test.c" -ldl -o "$build/usb-host-v2-test"
"$build/usb-host-v2-test" "$build/usb-host-v2.so"
cc -std=c11 -Wall -Wextra -Werror -I"$repo/sdk/driver" \
  "$repo/test/drivers/usb_host_checked_release_test.c" -ldl \
  -o "$build/usb-host-checked-release-test"
"$build/usb-host-checked-release-test" "$build/usb-host-v2.so"
# Device-recipient WCH vendor control must require a live claim; discovery
# faults must not bypass physical quiescence or pin verified-idle VBUS forever.
cc -std=c11 -Wall -Wextra -Werror -I"$repo/sdk/driver" \
  "$repo/test/drivers/usb_host_control_claim_test.c" -ldl \
  -o "$build/usb-host-control-claim-test"
"$build/usb-host-control-claim-test" "$build/usb-host-v2.so"
# Independent class ELFs on composite devices must not authorize one another's
# interface controls or device-wide vendor commands. Test the production ELF.
cc -std=c11 -Wall -Wextra -Werror -I"$repo/sdk/driver" \
  "$repo/test/drivers/usb_host_control_scope_test.c" -ldl \
  -o "$build/usb-host-control-scope-test"
"$build/usb-host-control-scope-test" "$build/usb-host-v2.so"
# One provider-owned event/identity snapshot, not a firmware descriptor loop.
# Physical presence survives transient identity failure; fault never looks
# like an empty healthy inventory.
cc -std=c11 -Wall -Wextra -Werror -I"$repo/sdk/driver" \
  "$repo/test/drivers/usb_host_snapshot_test.c" -ldl \
  -o "$build/usb-host-snapshot-test"
"$build/usb-host-snapshot-test" "$build/usb-host-v2.so"
exports="$(nm -D --defined-only "$build/usb-host-v2.so" | awk '{print $3}')"
[[ "$exports" == "t5_driver_get" ]] || { echo "Unexpected host ELF exports: $exports" >&2; exit 1; }
if nm -D --undefined-only "$build/usb-host-v2.so" | grep -E 'usb_host_|nativeUsb|UsbCdcDriverRuntime|t5_usb_'; then
  echo 'Host ELF forwards to forbidden compiled firmware USB implementation' >&2
  exit 1
fi

# The physical controller compiles this same two-stage release policy inside
# its ELF. Exercise it independently from unavailable hardware/IDF dependencies.
c++ -std=c++17 -Wall -Wextra -Werror -I"$repo/Drivers/usb_controller_esp32s3" \
  "$repo/test/drivers/usb_controller_claim_release_test.cpp" \
  -o "$build/usb-controller-claim-release-test"
"$build/usb-controller-claim-release-test"

# Guard actual production source wiring: timeout drain, retained retry state,
# and VBUS release must all survive subsequent provider refactors.
python3 "$repo/test/drivers/usb_controller_bulk_drain_source_test.py"
