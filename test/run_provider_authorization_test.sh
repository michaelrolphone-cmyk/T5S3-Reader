#!/usr/bin/env bash
set -euo pipefail
repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build="$(mktemp -d)"
trap 'rm -rf "$build"' EXIT
c++ -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer \
  -I"$repo/src" -I"$repo/lib/NativeApps/include" \
  "$repo/test/resources/provider_authorization_test.cpp" -o "$build/provider-authorization"
"$build/provider-authorization"
