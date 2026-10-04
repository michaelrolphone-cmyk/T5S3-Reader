#!/usr/bin/env bash
set -euo pipefail
repo_dir="$(cd "$(dirname "$0")/.." && pwd)"
work_dir="$(mktemp -d)"
trap 'rm -rf "$work_dir"' EXIT
mkdir "$work_dir/generated"
python3 "$repo_dir/scripts/gen_i18n.py" "$repo_dir/lib/I18n/translations" "$work_dir/generated" > "$work_dir/i18n.log"
flags=(-std=c++17 -Wall -Wextra -Werror -Wno-unused-function)
if [[ "${SANITIZE:-0}" == 1 ]]; then
  flags+=(-fsanitize=address,undefined -fno-omit-frame-pointer)
fi
"${CXX:-c++}" "${flags[@]}" \
  -I"$repo_dir/test/language_migration/stubs" -I"$work_dir/generated" \
  -I"$repo_dir/src" -I"$repo_dir/lib/Serialization" \
  "${SETTINGS_SOURCE:-$repo_dir/src/CrossPointSettings.cpp}" \
  "$repo_dir/test/language_migration/test.cpp" -o "$work_dir/test"
"$work_dir/test"
