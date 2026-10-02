#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."

tmpdir="$(mktemp -d)"
trap 'rm -rf "$tmpdir"' EXIT

c++ -std=c++17 -Wall -Wextra -Werror -Isrc/native \
  test/native_apps/font_catalog_validation_test.cpp \
  -o "$tmpdir/font_catalog_validation_test"
"$tmpdir/font_catalog_validation_test"
python3 test/native_apps/font_catalog_validation_source_test.py
