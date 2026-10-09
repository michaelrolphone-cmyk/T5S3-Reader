#!/usr/bin/env bash
set -euo pipefail
repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
runtime="${USB_CONTROLLER_RUNTIME_SOURCE:?Select the exact Runtime source to test}"
cmp "$runtime/sdk/driver/RiscUsbPhyResourceV1.h" "$repo/sdk/driver/RiscUsbPhyResourceV1.h"
build="$(mktemp -d)";trap 'rm -rf "$build"' EXIT
flags=(-std=c++17 -Wall -Wextra -Werror -Wno-missing-field-initializers)
if [[ "${SANITIZE:-0}" == 1 ]];then flags+=(-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -fno-pie -no-pie -g);fi
includes=(-I"$runtime/src" -I"$runtime/sdk/app" -I"$runtime/sdk/driver" -I"$runtime/sdk/hardware" -I"$runtime/lib/ArduinoJson/src" -I"$runtime/test/drivers/stubs" -I"$repo/Drivers/usb_controller_esp32s3")
sources=("$runtime/src/bootstrap/Json.cpp" "$runtime/src/bootstrap/Board.cpp" "$runtime/src/bootstrap/Runtime.cpp" "$runtime/src/runtime/streams/AppStreamSessions.cpp" "$runtime/src/runtime/streams/ProviderQueueHost.cpp" "$runtime/src/runtime/drivers/ProviderGraphV2.cpp" "$runtime/src/runtime/drivers/ProviderModuleV2.cpp" "$runtime/src/ports/esp32s3/CpuPort.cpp")
"${CXX:-c++}" "${flags[@]}" -rdynamic "${includes[@]}" "${sources[@]}" "$repo/test/drivers/controller_native_phy_runtime_test.cpp" -ldl -o "$build/runtime"
"$build/runtime"
python3 "$repo/test/drivers/controller_native_phy_start_test.py"
for selected in 0 1;do
 "${CXX:-c++}" "${flags[@]}" -DRISC_USB_CONTROLLER_NATIVE_PHY_LEASE=$selected -I"$repo/sdk/driver" "$repo/test/drivers/controller_host_startup_test.cpp" -o "$build/startup"
 "$build/startup"
 USB_CONTROLLER_NATIVE_PHY_LEASE=$selected python3 "$repo/test/drivers/controller_quiesce_path_test.py"
done
