#!/usr/bin/env bash
set -euo pipefail
repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build="$(mktemp -d)"
trap 'rm -rf "$build"' EXIT
c++ -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer \
  -I"$repo/lib/NativeApps/include" -I"$repo/src" \
  "$repo/src/runtime/streams/StreamRuntime.cpp" "$repo/test/streams/runtime_test.cpp" -o "$build/test"
"$build/test"
printf '#include "T5StreamApi.h"\nint main(void) { return T5_STREAM_API_VERSION != 1; }\n' > "$build/abi.c"
cc -std=c11 -Wall -Wextra -Werror -I"$repo/lib/NativeApps/include" "$build/abi.c" -o "$build/abi"
"$build/abi"
