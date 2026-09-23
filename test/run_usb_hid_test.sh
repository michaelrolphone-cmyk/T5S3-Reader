#!/usr/bin/env bash
set -euo pipefail
repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build="$(mktemp -d)"
trap 'rm -rf "$build"' EXIT
for class in usb_hid usb_hid_keyboard usb_hid_gamepad usb_xinput_gamepad usb_ui_navigation; do
  cc -std=c11 -Wall -Wextra -Werror -fPIC -fvisibility=hidden -shared \
    -I"$repo/sdk/driver" "$repo/Drivers/$class/driver.c" \
    -o "$build/$class.so"
  exports="$(nm -D --defined-only "$build/$class.so" | awk '{print $3}')"
  [[ "$exports" == "t5_driver_get" ]] || {
    echo "Unexpected $class ELF exports: $exports" >&2; exit 1;
  }
  if nm -D --undefined-only "$build/$class.so" | grep -E 'usb_host_|nativeUsb|UsbCdcDriverRuntime|t5_usb_'; then
    echo "$class incorrectly imports the firmware USB implementation" >&2
    exit 1
  fi
done
cc -std=c11 -Wall -Wextra -Werror -I"$repo/sdk/driver" \
  "$repo/test/drivers/usb_hid_test.c" -ldl -o "$build/hid-test"
"$build/hid-test" "$build/usb_hid.so" \
  "$build/usb_hid_keyboard.so" "$build/usb_hid_gamepad.so"
cc -std=c11 -Wall -Wextra -Werror -I"$repo/sdk/driver" \
  "$repo/test/drivers/usb_hid_gamepad_descriptor_test.c" -o "$build/hid-descriptor-test"
"$build/hid-descriptor-test"
cc -std=c11 -Wall -Wextra -Werror -I"$repo/sdk/driver" \
  "$repo/test/drivers/usb_xinput_gamepad_test.c" -o "$build/xinput-test"
"$build/xinput-test"
cc -std=c11 -Wall -Wextra -Werror -I"$repo/sdk/driver" \
  "$repo/test/drivers/usb_hid_quiesce_test.c" -ldl -o "$build/hid-quiesce-test"
"$build/hid-quiesce-test" "$build/usb_hid.so" \
  "$build/usb_hid_keyboard.so" "$build/usb_hid_gamepad.so"
cc -std=c11 -Wall -Wextra -Werror \
  "$repo/test/native_apps/text_editor_core_test.c" -o "$build/text-editor-test"
"$build/text-editor-test"

cc -std=c11 -Wall -Wextra -Werror \
  -I"$repo/lib/NativeApps/include" -I"$repo/sdk/driver" \
  "$repo/test/native_apps/text_editor_ui_test.c" -o "$build/text-editor-ui-test"
"$build/text-editor-ui-test"

c++ -std=c++17 -Wall -Wextra -Werror \
  "$repo/test/drivers/controller_interrupt_test.cpp" -o "$build/controller-interrupt-test"
"$build/controller-interrupt-test"

c++ -std=c++11 -Wall -Wextra -Werror -I"$repo/sdk/driver" \
  "$repo/test/drivers/controller_role_switch_test.cpp" -o "$build/controller-role-test"
"$build/controller-role-test"

cc -std=c11 -Wall -Wextra -Werror -I"$repo/sdk/driver" \
  "$repo/test/drivers/usb_ui_navigation_test.c" -o "$build/navigation-test"
"$build/navigation-test"
c++ -std=c++11 -Wall -Wextra -Werror -I"$repo/sdk/driver" \
  "$repo/test/drivers/navigation_focus_test.cpp" -o "$build/navigation-focus-test"
"$build/navigation-focus-test"
c++ -std=c++17 -Wall -Wextra -Werror -I"$repo/sdk/driver" -I"$repo/src" \
  -I"$repo/test/streams/stubs" -I"$repo/test/drivers/stubs" \
  "$repo/test/drivers/native_navigation_input_test.cpp" -o "$build/native-navigation-test"
"$build/native-navigation-test"
