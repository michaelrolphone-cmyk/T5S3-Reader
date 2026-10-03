#!/usr/bin/env bash
set -euo pipefail
repo_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
binary="$(mktemp)"
trap 'rm -f "$binary"' EXIT
c++ -std=c++17 -Wall -Wextra -Werror \
  -I"$repo_dir/test/clock_sync/stubs" -I"$repo_dir/src" \
  "$repo_dir/src/ClockSync.cpp" \
  "$repo_dir/test/clock_sync/ClockSyncRetryTest.cpp" -o "$binary"
"$binary"
echo 'Clock sync RTC write failure/retry regression passed'
c++ -std=c++17 -Wall -Wextra -Werror \
  -I"$repo_dir/test/clock_sync/stubs" -I"$repo_dir/src" \
  "$repo_dir/src/ClockSync.cpp" \
  "$repo_dir/test/clock_sync/ClockSyncRolloverTest.cpp" -o "$binary"
"$binary"
