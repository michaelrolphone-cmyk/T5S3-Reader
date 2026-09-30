#!/usr/bin/env bash
set -euo pipefail
repo_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
binary="$(mktemp)"
trap 'rm -f "$binary"' EXIT
# Artifact callers run once per app/provider. Validate the actual ELF here;
# the no-argument CI host job below runs the complete regression suite once.
# Do not recompile/re-run every game's tests for each unrelated artifact.
if [[ $# -gt 0 ]]; then
  if [[ $# -ne 1 ]]; then
    echo 'Usage: run_native_app_test.sh [ELF]' >&2
    exit 2
  fi
  cc -std=c11 -Wall -Wextra -Werror -I"$repo_dir/test/native_apps/stubs" \
    -I"$repo_dir/lib/elf_loader/include" \
    "$repo_dir/lib/elf_loader/src/esp_elf_validate.c" "$repo_dir/test/native_apps/validate_test.c" -o "$binary"
  echo "Validating native ELF: $1"
  timeout --kill-after=5s 60s "$binary" "$1"
  exit 0
fi
cc -std=c11 -Wall -Wextra -Werror -I"$repo_dir/test/native_apps/stubs" \
  -I"$repo_dir/lib/NativeApps/include" \
  "$repo_dir/lib/NativeApps/src/NativeAppLauncher.c" \
  "$repo_dir/test/native_apps/programmer_api_stub.c" \
  "$repo_dir/test/native_apps/compat_registration_stub.c" \
  "$repo_dir/test/native_apps/launcher_test.c" -o "$binary"
"$binary"
c++ -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined -I"$repo_dir/src" \
  "$repo_dir/test/resources/app_allocation_test.cpp" -o "$binary"
"$binary"
python3 "$repo_dir/test/hal/epd_lifecycle_test.py"
python3 "$repo_dir/test/hal/video_teardown_test.py"
python3 "$repo_dir/test/native_apps/app_memory_bridge_test.py"
python3 "$repo_dir/test/native_apps/detached_reader_glyph_test.py"
c++ -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined \
  -I"$repo_dir/test/native_apps/reader_stubs" -I"$repo_dir/sdk/driver" -I"$repo_dir/lib/GfxRenderer" \
  "$repo_dir/test/native_apps/reader_typography_test.cpp" -o "$binary"
"$binary"
c++ -std=c++17 -Wall -Wextra -Werror \
  "$repo_dir/test/native_apps/reader_page_layout_test.cpp" -o "$binary"
"$binary"
c++ -std=c++17 -O2 -Wall -Wextra -Werror \
  "$repo_dir/test/native_apps/native_video_mono_test.cpp" -o "$binary"
"$binary"
c++ -std=c++17 -Wall -Wextra -Werror \
  "$repo_dir/test/native_apps/native_video_gray_test.cpp" -o "$binary"
"$binary"
c++ -std=c++17 -Wall -Wextra -Werror \
  -I"$repo_dir/lib/NativeApps/include" \
  "$repo_dir/test/native_apps/native_video_profile_test.cpp" -o "$binary"
"$binary"
c++ -std=c++17 -Wall -Wextra -Werror \
  "$repo_dir/test/native_apps/native_video_idle_test.cpp" -o "$binary"
"$binary"
cc -std=c11 -O1 -g -Wall -Wextra -Werror -Wno-unused-function -fsanitize=address,undefined \
  -I"$repo_dir/lib/NativeApps/include" \
  "$repo_dir/test/native_apps/hollow_trail_trees_test.c" -o "$binary"
"$binary"
cc -std=c11 -O1 -g -Wall -Wextra -Werror -Wno-unused-function -fsanitize=address,undefined \
  -I"$repo_dir/lib/NativeApps/include" \
  "$repo_dir/test/native_apps/hollow_trail_forest_test.c" -o "$binary"
"$binary"
cc -std=c11 -O1 -g -Wall -Wextra -Werror -Wno-unused-function -fsanitize=address,undefined \
  -I"$repo_dir/lib/NativeApps/include" \
  "$repo_dir/test/native_apps/hollow_trail_direction_test.c" -o "$binary"
"$binary"
cc -std=c11 -O2 -Wall -Wextra -Werror -Wno-unused-function \
  -I"$repo_dir/lib/NativeApps/include" \
  "$repo_dir/test/native_apps/hollow_trail_renderer_test.c" -o "$binary"
"$binary"
cc -std=c11 -O2 -Wall -Wextra -Werror -Wno-unused-function \
  -I"$repo_dir/lib/NativeApps/include" \
  "$repo_dir/test/native_apps/hollow_trail_expanded_simd_test.c" -o "$binary"
"$binary"
cc -std=c11 -O2 -Wall -Wextra -Werror -Wno-unused-function \
  -I"$repo_dir/lib/NativeApps/include" \
  "$repo_dir/test/native_apps/hollow_trail_simd_test.c" -o "$binary"
"$binary"
cc -std=c11 -O2 -Wall -Wextra -Werror -Wno-unused-function \
  -I"$repo_dir/lib/NativeApps/include" \
  "$repo_dir/test/native_apps/hollow_trail_test.c" -o "$binary"
"$binary"
cc -std=c11 -O2 -Wall -Wextra -Werror -Wno-unused-function \
  -I"$repo_dir/lib/NativeApps/include" \
  "$repo_dir/test/native_apps/hollow_trail_cache_test.c" -o "$binary"
"$binary"
cc -std=c11 -O2 -Wall -Wextra -Werror -Wno-unused-function \
  -I"$repo_dir/lib/NativeApps/include" \
  "$repo_dir/test/native_apps/hollow_trail_composite_test.c" -o "$binary"
"$binary"
cc -std=c11 -O2 -Wall -Wextra -Werror -Wno-unused-function \
  -I"$repo_dir/lib/NativeApps/include" \
  "$repo_dir/test/native_apps/hollow_trail_grotto_test.c" -o "$binary"
"$binary"
cc -std=c11 -O1 -g -Wall -Wextra -Werror -Wno-unused-function -fsanitize=address,undefined \
  -I"$repo_dir/lib/NativeApps/include" -I"$repo_dir/sdk/driver" \
  "$repo_dir/test/native_apps/hollow_trail_controls_test.c" -o "$binary"
"$binary"
cc -std=c11 -O1 -g -Wall -Wextra -Werror -Wno-unused-function -fsanitize=address,undefined \
  -I"$repo_dir/lib/NativeApps/include" -I"$repo_dir/sdk/driver" \
  "$repo_dir/test/native_apps/hollow_trail_ending_test.c" -o "$binary"
"$binary"
cc -std=c11 -O2 -Wall -Wextra -Werror -Wno-unused-function \
  -I"$repo_dir/lib/NativeApps/include" -I"$repo_dir/sdk/driver" \
  "$repo_dir/test/native_apps/hollow_trail_pipeline_test.c" -o "$binary"
"$binary"
cc -std=c11 -O2 -Wall -Wextra -Werror -Wno-unused-function \
  -I"$repo_dir/lib/NativeApps/include" -I"$repo_dir/sdk/driver" \
  "$repo_dir/test/native_apps/hollow_trail_render_service_test.c" -o "$binary"
"$binary"
cc -std=c11 -O2 -Wall -Wextra -Werror -Wno-unused-function \
  -I"$repo_dir/lib/NativeApps/include" -I"$repo_dir/sdk/driver" \
  "$repo_dir/test/native_apps/game_dither_test.c" -o "$binary"
"$binary"
c++ -std=c++17 -O2 -Wall -Wextra -Werror -Wno-unused-function \
  -I"$repo_dir/lib/NativeApps/include" -I"$repo_dir/test/native_apps/math_stubs" \
  "$repo_dir/test/native_apps/native_math_test.cpp" -o "$binary"
"$binary"
# The real firmware device ABI bridge must authorize by execution context and
# never turn manifest compatibility or observation into a permission grant.
c++ -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer \
  -I"$repo_dir/src" -I"$repo_dir/lib/NativeApps/include" \
  "$repo_dir/src/native/NativeDeviceBridge.cpp" \
  "$repo_dir/test/resources/device_bridge_v2_test.cpp" -o "$binary"
"$binary"
cc -std=c11 -Wall -Wextra -Werror \
  "$repo_dir/lib/NativeApps/src/UnsignedDivisionCompat.c" \
  "$repo_dir/test/native_apps/unsigned_division_test.c" -o "$binary"
"$binary"
cc -std=c11 -Wall -Wextra -Werror \
  "$repo_dir/lib/NativeApps/src/SingleFloatDivisionCompat.c" \
  "$repo_dir/test/native_apps/single_float_division_test.c" -o "$binary"
"$binary"
python3 "$repo_dir/test/native_apps/test_symbols.py"
python3 "$repo_dir/test/native_apps/test_elf_cache_sync.py"
python3 "$repo_dir/test/native_apps/test_capability_manifest.py"
python3 "$repo_dir/test/resources/installed_provider_app_requirement_source_test.py"
python3 "$repo_dir/test/native_apps/network_cookie_session_source_test.py"
python3 "$repo_dir/test/native_apps/home_shortcut_launch_contract_test.py"
python3 "$repo_dir/test/native_apps/file_browser_retirement_contract_test.py"
python3 "$repo_dir/test/native_apps/model_viewer_contract_test.py"
MV_SANITIZE=1 python3 "$repo_dir/test/native_apps/model_viewer_shading_test.py"
MV_SANITIZE=1 python3 "$repo_dir/test/native_apps/model_viewer_controls_test.py"
MV_SANITIZE=1 python3 "$repo_dir/test/native_apps/model_viewer_preview_test.py"
MV_SANITIZE=1 python3 "$repo_dir/test/native_apps/catalog_freshness_test.py"
python3 "$repo_dir/test/resources/driver_install_stack_progress_source_test.py"
python3 "$repo_dir/test/native_apps/scheduled_bug_fix_behavior_test.py"
python3 "$repo_dir/test/native_apps/app_store_release_transition_source_test.py"
python3 "$repo_dir/test/native_apps/font_selection_persistence_source_test.py"
python3 "$repo_dir/test/native_apps/font_update_crc_source_test.py"
python3 "$repo_dir/test/native_apps/rom_manager_actions_touch_source_test.py"
python3 "$repo_dir/test/native_apps/text_editor_discard_source_test.py"
python3 "$repo_dir/test/native_apps/wifi_settings_cookie_source_test.py"
python3 "$repo_dir/test/native_apps/font_manager_confirm_edge_source_test.py"
bash "$repo_dir/test/run_serial_launch_contract.sh"
cc -std=c11 -Wall -Wextra -Werror \
  "$repo_dir/test/native_apps/gnss_consent_contract_test.c" -o "$binary"
(cd "$repo_dir" && "$binary")
cc -std=c11 -Wall -Wextra -Werror \
  -I"$repo_dir/lib/NativeApps/include" -I"$repo_dir/sdk/driver" \
  "$repo_dir/test/native_apps/usb_debug_test.c" -o "$binary"
(cd "$repo_dir" && "$binary")
cc -std=c11 -Wall -Wextra -Werror \
  -I"$repo_dir/lib/NativeApps/include" -I"$repo_dir/sdk/driver" \
  "$repo_dir/Apps/file_browser.c" "$repo_dir/test/native_apps/file_browser_test.c" -o "$binary"
(cd "$repo_dir" && "$binary")
cc -std=c11 -Wall -Wextra -Werror \
  -I"$repo_dir/lib/NativeApps/include" \
  "$repo_dir/test/native_apps/rom_manager_vimm_parser_test.c" -o "$binary"
(cd "$repo_dir" && "$binary")
echo 'Native app launcher tests passed'

bash "$repo_dir/test/run_panic_capture_test.sh"
