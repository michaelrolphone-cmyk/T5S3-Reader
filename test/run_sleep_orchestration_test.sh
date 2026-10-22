#!/usr/bin/env bash
set -euo pipefail
repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build="$(mktemp -d)"
trap 'rm -rf "$build"' EXIT
flags=(-std=c++17 -Wall -Wextra -Werror)
python_flags=()
if [[ "${SANITIZE:-0}" == 1 ]]; then
  flags+=(-fsanitize=address,undefined -fno-omit-frame-pointer)
  python_flags+=(--sanitize)
fi
"${CXX:-c++}" "${flags[@]}" -I"$repo/src" \
  "$repo/src/runtime/power/SleepOrchestration.cpp" \
  "$repo/test/sleep_orchestration/orchestration_test.cpp" -o "$build/test"
"$build/test"
python3 "$repo/test/sleep_orchestration/reader_parity_test.py" "${python_flags[@]}"
