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
python3 "$repo_dir/test/text_page_writes/text_page_writes_test.py" --sanitize
python3 "$repo_dir/test/text_page_writes/text_page_writes_test.py" --legacy
python3 "$repo_dir/test/activities/txt_index_io_budget_test.py" --sanitize
python3 "$repo_dir/test/activities/txt_index_io_budget_test.py" --legacy
python3 "$repo_dir/test/debug_serial/run_test.py" --sanitize
python3 "$repo_dir/test/debug_serial/run_test.py" --sanitize --negative-control
BOOKMARK_TEST_SANITIZERS=1 bash "$repo_dir/test/run_bookmark_summary_test.sh"
BOOKMARK_TOGGLE_SANITIZE=1 python3 "$repo_dir/test/bookmark_toggle/toggle_test.py"
python3 "$repo_dir/test/native_apps/elf_section_layout_test.py"
WRAP_TEST_SANITIZE=1 python3 "$repo_dir/test/text_wrap_regression.py"
python3 "$repo_dir/test/title_truncation/truncation_test.py" --sanitize --enforce-cost
python3 "$repo_dir/test/koreader_document_id/document_id_test.py" --sanitize
python3 "$repo_dir/test/epub_anchor_reads/run_test.py" --sanitize --board x4
python3 "$repo_dir/test/epub_anchor_reads/run_test.py" --sanitize --board t5
python3 "$repo_dir/test/epub_anchor_reads/run_test.py" --sanitize --negative-control
python3 "$repo_dir/test/epub_css_cache_io/run_test.py" --sanitize --board x4
python3 "$repo_dir/test/epub_css_cache_io/run_test.py" --sanitize --board t5
python3 "$repo_dir/test/epub_css_cache_io/run_test.py" --sanitize --negative-control
bash "$repo_dir/test/run_epub_metadata_finalization_io_test.sh" --board x4 --sanitize
bash "$repo_dir/test/run_epub_metadata_finalization_io_test.sh" --board t5 --sanitize
bash "$repo_dir/test/run_epub_metadata_finalization_io_test.sh" --board x4 --sanitize --negative-control
cc -std=c11 -Wall -Wextra -Werror -I"$repo_dir/test/native_apps/stubs" \
  -I"$repo_dir/lib/NativeApps/include" \
  "$repo_dir/lib/NativeApps/src/NativeAppLauncher.c" \
  "$repo_dir/test/native_apps/programmer_api_stub.c" \
  "$repo_dir/test/native_apps/compat_registration_stub.c" \
  "$repo_dir/test/native_apps/launcher_test.c" -o "$binary"
"$binary"
"$binary" reader
"$binary" child-close
python3 "$repo_dir/test/reader_entry/run_tests.py"
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
  "$repo_dir/test/native_apps/hollow_trail_character_test.c" -o "$binary"
"$binary"
cc -std=c11 -O1 -g -Wall -Wextra -Werror -Wno-unused-function -fsanitize=address,undefined \
  -I"$repo_dir/lib/NativeApps/include" \
  "$repo_dir/test/native_apps/hollow_trail_framing_test.c" -o "$binary"
"$binary"
cc -std=c11 -O1 -g -Wall -Wextra -Werror -Wno-unused-function -fsanitize=address,undefined \
  -I"$repo_dir/lib/NativeApps/include" \
  "$repo_dir/test/native_apps/hollow_trail_ground_test.c" -o "$binary"
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
cc -std=c11 -O1 -g -Wall -Wextra -Werror -Wno-unused-function -fsanitize=address,undefined \
  -I"$repo_dir/lib/NativeApps/include" \
  "$repo_dir/test/native_apps/hollow_trail_cutscene_test.c" -o "$binary"
"$binary"
cc -std=c11 -O1 -g -Wall -Wextra -Werror -Wno-unused-function -fsanitize=address,undefined \
  -I"$repo_dir/lib/NativeApps/include" \
  "$repo_dir/test/native_apps/hollow_trail_schoolroom_test.c" -o "$binary"
"$binary"
cc -std=c11 -O1 -g -Wall -Wextra -Werror -Wno-unused-function -fsanitize=address,undefined \
  -I"$repo_dir/lib/NativeApps/include" \
  "$repo_dir/test/native_apps/hollow_trail_signal_room_test.c" -o "$binary"
"$binary"
cc -std=c11 -O1 -g -Wall -Wextra -Werror -Wno-unused-function -fsanitize=address,undefined \
  -I"$repo_dir/lib/NativeApps/include" \
  "$repo_dir/test/native_apps/hollow_trail_relay_test.c" -o "$binary"
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
python3 "$repo_dir/test/native_apps/file_browser_oversize_usb_handle_source_test.py"
python3 "$repo_dir/test/native_apps/file_browser_oversize_usb_handle_runtime_test.py"
python3 "$repo_dir/test/native_apps/model_viewer_contract_test.py"
MV_SANITIZE=1 python3 "$repo_dir/test/native_apps/model_viewer_shading_test.py"
MV_SANITIZE=1 python3 "$repo_dir/test/native_apps/model_viewer_controls_test.py"
MV_SANITIZE=1 python3 "$repo_dir/test/native_apps/model_viewer_preview_test.py"
MV_SANITIZE=1 python3 "$repo_dir/test/native_apps/catalog_freshness_test.py"
MV_SANITIZE=1 python3 "$repo_dir/test/native_apps/ordinary_catalog_refresh_test.py"
MV_SANITIZE=1 python3 "$repo_dir/test/native_apps/managed_app_identity_test.py"
python3 "$repo_dir/test/resources/driver_install_stack_progress_source_test.py"
python3 "$repo_dir/test/native_apps/scheduled_bug_fix_behavior_test.py"
python3 "$repo_dir/test/native_apps/app_store_release_transition_source_test.py"
python3 "$repo_dir/test/native_apps/font_selection_persistence_source_test.py"
python3 "$repo_dir/test/native_apps/button_remap_runtime_test.py"
python3 "$repo_dir/test/native_apps/font_update_crc_source_test.py"
python3 "$repo_dir/test/native_apps/rom_manager_actions_touch_source_test.py"
python3 "$repo_dir/test/native_apps/text_editor_discard_source_test.py"
cc -std=c11 -Wall -Wextra -Werror \
  -I"$repo_dir/lib/NativeApps/include" -I"$repo_dir/sdk/driver" \
  "$repo_dir/test/native_apps/text_editor_open_test.c" -o "$binary"
"$binary"
python3 "$repo_dir/test/native_apps/wifi_settings_cookie_source_test.py"
python3 "$repo_dir/test/native_apps/font_manager_confirm_edge_source_test.py"
python3 "$repo_dir/test/native_apps/timecard_clock_failure_source_test.py"
cc -std=c11 -Wall -Wextra -Werror -I"$repo_dir/lib/NativeApps/include" \
  "$repo_dir/test/native_apps/timecard_clock_failure_test.c" -o "$binary"
"$binary"
cc -std=c11 -Wall -Wextra -Werror \
  -I"$repo_dir/lib/NativeApps/include" -I"$repo_dir/sdk/driver" \
  "$repo_dir/test/native_apps/firmware_flasher_pagination_test.c" -o "$binary"
(cd "$repo_dir" && "$binary")
python3 "$repo_dir/test/native_apps/timecard_store_failure_source_test.py"
cc -std=c11 -Wall -Wextra -Werror -I"$repo_dir/lib/NativeApps/include" \
  "$repo_dir/test/native_apps/timecard_store_failure_test.c" -o "$binary"
"$binary"
bash "$repo_dir/test/run_serial_launch_contract.sh"
cc -std=c11 -Wall -Wextra -Werror \
  "$repo_dir/test/native_apps/gnss_consent_contract_test.c" -o "$binary"
(cd "$repo_dir" && "$binary")
cc -std=c11 -Wall -Wextra -Werror \
  -I"$repo_dir/lib/NativeApps/include" -I"$repo_dir/sdk/driver" \
  "$repo_dir/test/native_apps/usb_debug_test.c" -o "$binary"
(cd "$repo_dir" && "$binary")
cc -std=c11 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined \
  -I"$repo_dir/lib/NativeApps/include" -I"$repo_dir/sdk/driver" \
  "$repo_dir/test/native_apps/file_browser_test.c" -o "$binary"
(cd "$repo_dir" && "$binary")
cc -std=c11 -Wall -Wextra -Werror \
  -I"$repo_dir/lib/NativeApps/include" \
  "$repo_dir/test/native_apps/rom_manager_vimm_parser_test.c" -o "$binary"
(cd "$repo_dir" && "$binary")
python3 "$repo_dir/test/native_apps/confirmation_input_test.py"
echo '== Canonical app owned-buffer and invocation metadata admission =='
c++ -std=c++17 -Wall -Wextra -Werror -Wno-overloaded-virtual -fsanitize=address,undefined \
  -I"$repo_dir/test/hal/storage_stubs" -I"$repo_dir/lib/hal" -I"$repo_dir/src" \
  -I"$repo_dir/test/resources/cdc_sd_stubs" \
  "$repo_dir/lib/hal/HalStorage.cpp" "$repo_dir/src/runtime/packages/PackageExecutableAdmission.cpp" \
  "$repo_dir/src/native/ManagedAppAdmission.cpp" "$repo_dir/test/resources/managed_app_admission_test.cpp" \
  -lcrypto -o "$binary"
"$binary"
python3 "$repo_dir/test/native_apps/elf_owned_admission_test.py"
python3 "$repo_dir/test/native_apps/sd_vfs_lock_test.py"
c++ -std=c++17 -Wall -Wextra -Werror \
  -I"$repo_dir/test/native_storage_stubs" -I"$repo_dir/lib/NativeApps/include" \
  -I"$repo_dir/src/native" -I"$repo_dir/src" \
  "$repo_dir/test/native_storage_read_test.cpp" -o "$binary"
"$binary"
c++ -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer \
  -I"$repo_dir/test/native_apps/sd_firmware_bridge_stubs" \
  -I"$repo_dir/lib/NativeApps/include" -I"$repo_dir/src/native" -I"$repo_dir/src" \
  "$repo_dir/test/native_apps/native_sd_firmware_bridge_test.cpp" -o "$binary"
"$binary"
python3 "$repo_dir/test/firmware_flasher/segment_limit_test.py" --sanitize
echo 'Native app launcher tests passed'

bash "$repo_dir/test/run_panic_capture_test.sh"
