#!/usr/bin/env bash
set -euo pipefail
repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build="$(mktemp -d)"
trap 'rm -rf "$build"' EXIT
cc -std=c11 -Wall -Wextra -Werror -fPIC -fvisibility=hidden -shared \
  -I"$repo/sdk/driver" -I"$repo/lib/NativeApps/include" \
  "$repo/Drivers/gps_nmea/driver.c" -o "$build/gps.so"
cc -std=c11 -Wall -Wextra -Werror -fPIC -shared -I"$repo/sdk/driver" \
  "$repo/test/drivers/bad_driver.c" -o "$build/bad.so"
c++ -std=c++17 -Wall -Wextra -Werror -I"$repo/sdk/driver" -I"$repo/lib/NativeApps/include" \
  -I"$repo/src" -I"$repo/test/drivers/stubs" "$repo/src/runtime/drivers/GpsDriverModule.cpp" \
  "$repo/test/drivers/module_test.cpp" -ldl -o "$build/test"
"$build/test" "$build/gps.so" "$build/bad.so"
c++ -std=c++17 -Wall -Wextra -Werror -I"$repo/src" \
  "$repo/test/resources/device_registry_test.cpp" -o "$build/device-registry-test"
"$build/device-registry-test"
# Unified package identity is a bounded, transport-independent contract for
# app, driver, service and provider metadata. It grants no executable trust.
c++ -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer \
  -I"$repo/src" "$repo/test/resources/package_identity_test.cpp" \
  -o "$build/package-identity-test"
"$build/package-identity-test"
# Reject malformed envelope metadata and unavailable ABI dependencies before
# reading/executing candidate code; version decisions are numeric and typed.
c++ -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer \
  -I"$repo/src" "$repo/test/resources/package_preflight_test.cpp" \
  -o "$build/package-preflight-test"
"$build/package-preflight-test"
# Reject duplicate JSON keys at every nesting level, escaped key aliases and
# malformed/unbounded input before ArduinoJson can overwrite a declaration.
c++ -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer \
  -I"$repo/src" "$repo/test/resources/package_json_guard_test.cpp" \
  -o "$build/package-json-guard-test"
"$build/package-json-guard-test"
# The shared directory transaction must retain the last verified generation
# through rename failures, abrupt resets, corrupt stages and cleanup failures.
c++ -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer \
  -I"$repo/src" "$repo/test/resources/package_transaction_test.cpp" \
  -o "$build/package-transaction-test"
"$build/package-transaction-test"
# Legacy flat App Store layout requires a two-file transaction while existing
# launch paths are preserved; inject failures at each rename/cleanup boundary.
c++ -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer \
  -I"$repo/src" "$repo/test/resources/package_pair_transaction_test.cpp" \
  -o "$build/package-pair-transaction-test"
"$build/package-pair-transaction-test"
# Interrupted cleanup can leave only part of a managed backup. Recheck the
# published target and retry managed-only cleanup; refuse unknown content.
c++ -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer \
  -I"$repo/src" "$repo/test/resources/package_recovery_test.cpp" \
  -o "$build/package-recovery-test"
"$build/package-recovery-test"
# Generic provider API metadata and transactionally bound non-authorizing
# dependencies must both pass before an ELF is mapped.
c++ -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer \
  -I"$repo/src" -I"$repo/lib/NativeApps/include" \
  "$repo/test/resources/app_capability_requirements_test.cpp" -o "$build/app-capability-requirements"
"$build/app-capability-requirements"
c++ -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer \
  -I"$repo/src" "$repo/test/resources/app_dependency_bindings_test.cpp" \
  -o "$build/app-dependency-bindings"
"$build/app-dependency-bindings"
c++ -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer \
  -I"$repo/src" "$repo/test/resources/app_dependency_lifecycle_test.cpp" \
  -o "$build/app-dependency-lifecycle"
"$build/app-dependency-lifecycle"
# A manifest must never grant its own rights. Only trusted owner-task grants
# can create scoped semantic handles, all reclaimed at invocation termination.
c++ -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer \
  -I"$repo/src" "$repo/test/resources/capability_access_test.cpp" \
  -o "$build/capability-access"
"$build/capability-access"
cc -std=c11 -Wall -Wextra -Werror -I"$repo/lib/NativeApps/include" \
  "$repo/test/resources/device_api_v2_abi_test.c" -o "$build/device-api-abi"
"$build/device-api-abi"
# Approval must require a fresh physical button edge: a held Confirm or a tap
# initiated under the app's earlier framebuffer cannot authorize hardware.
c++ -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer \
  -I"$repo/src" "$repo/test/resources/consent_input_gate_test.cpp" \
  -o "$build/consent-input-gate"
"$build/consent-input-gate"
c++ -std=c++17 -Wall -Wextra -Werror -I"$repo/src" \
  "$repo/test/resources/device_event_test.cpp" -o "$build/device-event-test"
"$build/device-event-test"
c++ -std=c++17 -Wall -Wextra -Werror -I"$repo/src" \
  "$repo/test/resources/device_subscription_test.cpp" -o "$build/device-subscription-test"
"$build/device-subscription-test"
c++ -std=c++17 -Wall -Wextra -Werror -I"$repo/src" \
  "$repo/test/resources/usb_serial_projection_test.cpp" -o "$build/usb-projection-test"
"$build/usb-projection-test"
c++ -std=c++17 -Wall -Wextra -Werror -I"$repo/src" \
  "$repo/test/programmer/esp_rom_protocol_test.cpp" -o "$build/esp-rom-protocol-test"
"$build/esp-rom-protocol-test"
c++ -std=c++17 -Wall -Wextra -Werror -I"$repo/src" -I"$repo/lib/NativeApps/include" \
  "$repo/test/programmer/esp_rom_session_test.cpp" -o "$build/esp-rom-session-test"
"$build/esp-rom-session-test"
# Real firmware provider, stubbed serial/streams/MD5; the baseline is booted
# with a real execution context, and the second binary injects app stop/exit.
programmer_flags=(-std=c++17 -Wall -Wextra -Werror
  -I"$repo/test/programmer/stubs" -I"$repo/lib/NativeApps/include" -I"$repo/src")
c++ "${programmer_flags[@]}" "$repo/src/native/NativeEspRomBridge.cpp" \
  "$repo/test/programmer/programmer_context_fixture.cpp" \
  "$repo/test/programmer/esp_rom_provider_test.cpp" -o "$build/esp-rom-provider-test"
"$build/esp-rom-provider-test"
c++ "${programmer_flags[@]}" "$repo/src/native/NativeEspRomBridge.cpp" \
  "$repo/test/programmer/esp_rom_context_test.cpp" -o "$build/esp-rom-context-test"
"$build/esp-rom-context-test"
c++ -std=c++17 -Wall -Wextra -Werror -DBOARD_T5S3_PRO \
  -I"$repo/test/drivers/stubs" -I"$repo/src" "$repo/src/runtime/resources/RadioPower.cpp" \
  "$repo/test/drivers/power_test.cpp" -o "$build/power-test"
"$build/power-test"
bash "$repo/test/run_usb_cdc_driver_test.sh"
python3 "$repo/test/drivers/package_test.py"
python3 "$repo/test/drivers/usb_package_test.py"
