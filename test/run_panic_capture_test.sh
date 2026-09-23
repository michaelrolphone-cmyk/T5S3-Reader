#!/usr/bin/env bash
set -euo pipefail
repo_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
binary="$(mktemp)"
trap 'rm -f "$binary"' EXIT
c++ -std=c++17 -Wall -Wextra -Werror -I"$repo_dir/test/panic/stubs" \
  -I"$repo_dir/lib/hal" "$repo_dir/test/panic/capture_test.cpp" -o "$binary"
"$binary"
c++ -std=c++17 -Wall -Wextra -Werror -I"$repo_dir/test/panic/stubs" \
  -I"$repo_dir/lib/Logging" "$repo_dir/lib/Logging/Logging.cpp" \
  "$repo_dir/test/panic/logging_test.cpp" -o "$binary"
"$binary"
