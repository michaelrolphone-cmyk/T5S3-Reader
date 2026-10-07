#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
build="$(mktemp -d)"; trap 'rm -rf "$build"' EXIT
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -pedantic -I"$root/sdk/driver" "$root/test/display_power/contract_test.c" -o "$build/test"
"$build/test"
printf '#include "RiscDisplayOutputPowerV1.h"\nint main() { return risc_display_output_power(nullptr) != nullptr; }\n' > "$build/cpp.cpp"
"${CXX:-c++}" -std=c++11 -Wall -Wextra -Werror -pedantic -I"$root/sdk/driver" "$build/cpp.cpp" -o "$build/cpp"
"$build/cpp"
echo 'display.output power suffix C/C++ and legacy-prefix discovery: PASS'
