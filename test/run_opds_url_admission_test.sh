#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build_dir="$(mktemp -d /tmp/opds-url-admission.XXXXXX)"
trap 'rm -rf "$build_dir"' EXIT

c++ -std=c++17 -Wall -Wextra -Werror \
  -I"$repo_root/lib/NativeApps/include" -I"$repo_root/src" \
  "$repo_root/src/native/NativeOpdsBridge.cpp" \
  "$repo_root/test/native_bridges/opds_url_admission_test.cpp" \
  -o "$build_dir/opds_url_admission_test"
"$build_dir/opds_url_admission_test"

cc -std=c11 -Wall -Wextra -Werror \
  -I"$repo_root/lib/NativeApps/include" \
  "$repo_root/Apps/opds_settings.c" \
  "$repo_root/test/native_apps/opds_settings_test.c" \
  -o "$build_dir/opds_settings_test"
"$build_dir/opds_settings_test"
