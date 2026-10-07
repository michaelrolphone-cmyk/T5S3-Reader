#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
build="$(mktemp -d)"; trap 'rm -rf "$build"' EXIT
flags=(-Wall -Wextra -Werror -pedantic -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer)
"${CC:-cc}" -std=c11 "${flags[@]}" -I"$root/sdk/driver" "$root/test/touch_power/test.c" -o "$build/c"
"$build/c"
"${CXX:-c++}" -x c++ -std=c++17 "${flags[@]}" -I"$root/sdk/driver" "$root/test/touch_power/test.c" -o "$build/cpp"
"$build/cpp"
