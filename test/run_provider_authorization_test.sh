#!/usr/bin/env bash
set -euo pipefail
repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build="$(mktemp -d)"
trap 'rm -rf "$build"' EXIT
flags=(-std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined
       -fno-omit-frame-pointer -I"$repo/src" -I"$repo/lib/NativeApps/include")
c++ "${flags[@]}" "$repo/test/resources/provider_authorization_test.cpp" \
  -o "$build/provider-authorization"
"$build/provider-authorization"
c++ "${flags[@]}" "$repo/test/resources/provider_mode_rights_test.cpp" \
  -o "$build/provider-mode-rights"
"$build/provider-mode-rights"
c++ "${flags[@]}" "$repo/test/resources/gnss_stream_authority_test.cpp" \
  -o "$build/gnss-stream-authority"
"$build/gnss-stream-authority"
# Link the REAL GpsDriverRuntime against a mocked driver ELF/kernel/stream
# boundary to verify that unsuccessful semantic attach releases the physical
# claim and context cleanup slot and permits a subsequent successful retry.
c++ "${flags[@]}" -I"$repo/test/drivers/stubs" -I"$repo/sdk/driver" \
  "$repo/src/runtime/drivers/GpsDriverRuntime.cpp" \
  "$repo/test/drivers/gnss_startup_failure_test.cpp" \
  -o "$build/gnss-startup-failure"
"$build/gnss-startup-failure"
