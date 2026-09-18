#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
cc -std=c11 -Wall -Wextra -Werror test/native_apps/serial_monitor_launch_contract_test.c -o "$work/serial-launch-contract"
"$work/serial-launch-contract"
echo 'Serial Monitor launch UI/retry contract: passed'
