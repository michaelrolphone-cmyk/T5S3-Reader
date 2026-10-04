#!/usr/bin/env bash
set -euo pipefail
repo_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
test_binary="$(mktemp)"
trap 'rm -f "$test_binary"' EXIT
c++ -std=c++17 -Wall -Wextra -Werror -I"$repo_dir/src" \
  "$repo_dir/test/desk_clock/DeskClockTimeTest.cpp" -o "$test_binary"
"$test_binary"
echo 'Desk clock minute alignment tests passed'
c++ -std=c++17 -Wall -Wextra -Werror -I"$repo_dir/src" \
  "$repo_dir/test/desk_clock/IdleSleepTest.cpp" -o "$test_binary"
"$test_binary"
c++ -std=c++17 -Wall -Wextra -Werror -I"$repo_dir/lib/hal" \
  "$repo_dir/test/desk_clock/ClockFormatTest.cpp" -o "$test_binary"
"$test_binary"
echo '12/24-hour clock format tests passed'
python3 "$repo_dir/test/desk_clock/DeepSleepWiringTest.py"
python3 "$repo_dir/test/desk_clock/SleepBoundaryTest.py"
python3 "$repo_dir/test/desk_clock/BootPowerTest.py"
python3 "$repo_dir/test/desk_clock/ResumeBehaviorTest.py"
echo 'Deep-sleep clock wiring tests passed'
preview_dir="$(mktemp -d)"
trap 'rm -f "$test_binary"; rm -rf "$preview_dir"' EXIT
c++ -std=c++17 -O2 -Wall -Wextra -Werror -I"$repo_dir/src" \
  "$repo_dir/scripts/preview_clock_faces.cpp" -o "$test_binary"
"$test_binary" "$preview_dir"
echo 'Clock face bounds and every-minute 12/24-hour rendering passed at both panel sizes'
