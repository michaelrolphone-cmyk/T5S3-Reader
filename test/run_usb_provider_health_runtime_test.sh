#!/usr/bin/env bash
# Run production Reader descriptors through the frozen Runtime .94 graph.
set -euo pipefail
reader="$(cd "$(dirname "$0")/.." && pwd)"
repo="${USB_HEALTH_RUNTIME_SOURCE:?Select the exact Runtime .94 source}"
[[ "$(git -C "$repo" rev-parse HEAD)" == 51a9a09e15789be4587f59295f89c3e3b5be0c66 ]]
git -C "$repo" diff --quiet HEAD -- src sdk test
cmp "$reader/sdk/driver/RiscProviderHealthV1.h" "$repo/sdk/driver/RiscProviderHealthV1.h"
build="$(mktemp -d)";trap 'rm -rf "$build"' EXIT
mkdir "$build/sdk"
cp "$reader/sdk/driver/"*.h "$build/sdk/"
for header in "$repo/sdk/driver/"*.h;do ln -sf "$header" "$build/sdk/$(basename "$header")";done
includes=(-I"$repo/src" -I"$repo/sdk/app" -I"$build/sdk" -I"$repo/sdk/hardware" -I"$repo/lib/ArduinoJson/src" -I"$repo/test/drivers/stubs")
san=();if [[ "${SANITIZE:-0}" == 1 ]];then san=(-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -g);fi
cflags=(-std=c11 -Wall -Wextra -Werror -fPIC -fvisibility=hidden -shared -Wl,-z,relro,-z,now)
fixture="$repo/test/fixtures/provider_resource_handoff.c"
cc "${cflags[@]}" "${san[@]}" "${includes[@]}" -DRESOURCE_PROVIDER -DRESOURCE_ACTUAL_CONTROLLER "$fixture" -o "$build/owner.elf"
cc "${cflags[@]}" "${san[@]}" "${includes[@]}" "$fixture" -o "$build/default.elf"
cc "${cflags[@]}" "${san[@]}" "${includes[@]}" -DRESOURCE_APP_ROLE=1 "$fixture" -o "$build/foreground.elf"
for provider in usb_host_v2 usb_hid;do
 cc "${cflags[@]}" "${san[@]}" "${includes[@]}" -Wl,--version-script,"$reader/Drivers/usb_controller_esp32s3/exports.health.map" "$reader/Drivers/$provider/driver.health.c" -o "$build/$provider.elf"
done
cc "${cflags[@]}" "${san[@]}" "${includes[@]}" "$reader/Drivers/usb_host_v2/driver.c" -o "$build/unknown.elf"
sources=("$repo/src/bootstrap/Json.cpp" "$repo/src/bootstrap/Board.cpp" "$repo/src/bootstrap/Runtime.cpp" "$repo/src/runtime/streams/AppStreamSessions.cpp" "$repo/src/runtime/streams/ProviderQueueHost.cpp" "$repo/src/runtime/drivers/ProviderGraphV2.cpp" "$repo/src/runtime/drivers/ProviderModuleV2.cpp" "$repo/src/ports/esp32s3/CpuPort.cpp")
c++ -std=c++17 -Wall -Wextra -Werror -Wno-missing-field-initializers -DRISC_TEST_ACTUAL_USB -fno-pie -no-pie -rdynamic "${san[@]}" "${includes[@]}" "${sources[@]}" "$repo/test/provider_resource_handoff_test.cpp" -ldl -o "$build/test"
cp "$build/usb_hid.elf" "$build/hid.elf"
for mode in actual-ready actual-retained actual-unknown;do
 cp "$build/usb_host_v2.elf" "$build/provider.elf"
 if [[ "$mode" == actual-unknown ]];then cp "$build/unknown.elf" "$build/provider.elf";fi
 "$build/test" "$build" "$mode"
done
