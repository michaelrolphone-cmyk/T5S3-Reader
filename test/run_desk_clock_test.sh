#!/usr/bin/env bash
set -euo pipefail
repo_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
test_binary="$(mktemp)"
trap 'rm -f "$test_binary"' EXIT
c++ -std=c++17 -Wall -Wextra -Werror -I"$repo_dir/src" \
  "$repo_dir/test/desk_clock/DeskClockTimeTest.cpp" -o "$test_binary"
"$test_binary"
echo 'Desk clock minute alignment tests passed'
