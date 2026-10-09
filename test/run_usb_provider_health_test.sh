#!/usr/bin/env bash
set -euo pipefail
repo="$(cd "$(dirname "$0")/.." && pwd)"
build="$(mktemp -d)";trap 'rm -rf "$build"' EXIT
flags=(-std=c11 -Wall -Wextra -Werror -g -I"$repo/sdk/driver")
if [[ "${SANITIZE:-0}" == 1 ]];then flags+=(-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer);fi
for provider in usb_host_v2 usb_hid usb_hid_keyboard usb_hid_mouse usb_hid_gamepad usb_xinput_gamepad usb_hid_text_input;do
 cc "${flags[@]}" -fPIC -fvisibility=hidden -shared -Wl,-z,relro,-z,now -Wl,--version-script,"$repo/Drivers/usb_controller_esp32s3/exports.health.map" "$repo/Drivers/$provider/driver.health.c" -o "$build/$provider.so"
 [[ "$(nm -D --defined-only "$build/$provider.so" | awk '{print $3}' | sort | tr '\n' ' ')" == 'risc_provider_health_v1_descriptor t5_driver_get ' ]]
done
cc "${flags[@]}" -no-pie "$repo/test/provider_health/stack.c" -ldl -o "$build/stack"
"$build/stack" "$build" ready
"$build/stack" "$build" retained
python3 "$repo/scripts/stage_usb_provider_health.py" "$build"
cxx=(-std=c++17 -Wall -Wextra -Werror -Wno-unused-function -DRISC_USB_CONTROLLER_NATIVE_PHY_LEASE=1 -I"$build" -I"$repo/Drivers/usb_controller_esp32s3" -I"$repo/test/provider_health/stubs" -I"$repo/sdk/driver")
if [[ "${SANITIZE:-0}" == 1 ]];then cxx+=(-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -g);fi
c++ "${cxx[@]}" -no-pie "$repo/test/provider_health/controller.cpp" -o "$build/controller"
for mode in ready busy control-retained bulk-retained admission-close-retained claimed-busy unknown stuck failed stall-detach device-exhaustion device-exhaustion-closed claim-exhaustion request-exhaustion vbus-tail power-release native-release fault-timeout;do "$build/controller" "$mode";done

cc "${flags[@]}" -no-pie "$repo/test/provider_health/facade.c" -ldl -o "$build/facade"
for kind in keyboard gamepad mouse text_input;do "$build/facade" "$build/usb_hid_$kind.so" "$kind";done

cc "${flags[@]}" -DRISC_TEST_PROVIDER_HEALTH -no-pie "$repo/test/drivers/usb_host_deadlines_test.c" -ldl -o "$build/deadlines"
for scenario in gate events inventory_fault valid partial_setup partial_claim duplicate_claim unknown_claim claim_overrun timeout cancel stale release_failure io_retained io_timeout io_overrun event_overrun clock_reversal scope invalid;do "$build/deadlines" "$build/usb_host_v2.so" "$scenario";done
