#!/usr/bin/env bash
set -euo pipefail
repo_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build_dir="$(mktemp -d)"
trap 'rm -rf "$build_dir"' EXIT
mkdir -p "$build_dir/sd/.fonts"
ln -s "$repo_dir/SD_fonts/FAClassicSolid" "$build_dir/sd/.fonts/FAClassicSolid"
ln -s "$repo_dir/SD_fonts/FAClassicRegular" "$build_dir/sd/.fonts/FAClassicRegular"
c++ -std=c++17 -O1 -Wall -Wextra -Werror \
  -I"$repo_dir/test/fontawesome/stubs" -I"$repo_dir/src" \
  -I"$repo_dir/lib/NativeApps/include" -I"$repo_dir/lib/EpdFont" -I"$repo_dir/lib/Utf8" \
  "$repo_dir/test/fontawesome/FontAwesomeTest.cpp" "$repo_dir/src/components/FontAwesomeIcons.cpp" \
  "$repo_dir/lib/EpdFont/SdCardFont.cpp" "$repo_dir/lib/EpdFont/EpdFont.cpp" \
  "$repo_dir/lib/EpdFont/EpdFontFamily.cpp" "$repo_dir/lib/Utf8/Utf8.cpp" -o "$build_dir/test"
"$build_dir/test" "$build_dir/sd"
echo 'Font Awesome tests passed with shipped cpfont files'
