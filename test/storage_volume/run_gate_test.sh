#!/usr/bin/env bash
set -euo pipefail
repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
build="$(mktemp -d)"
trap 'rm -rf "$build"' EXIT
cd "$repo"
for transport in x4 spi; do
  transport_flags=()
  scenarios=(lifetime start-take start-give non-owner isr-leave quiesce-race quiesce-give prepare-give cancel-give repeat-prepare-give)
  if [[ "$transport" == spi ]]; then
    transport_flags+=(-DTEST_SPI_TRANSPORT)
    scenarios+=(release-retained end-retained)
  else
    scenarios+=(commit-give repeat-commit-give)
  fi
  "${CC:-cc}" -std=c11 -D_XOPEN_SOURCE=700 -O1 -g -Wall -Wextra -Werror -Wno-overflow \
    -pthread -fsanitize=address,undefined -fno-omit-frame-pointer "${transport_flags[@]}" \
    -Itest/storage_volume/gate_fake -Isdk/driver -IDrivers/x4pro_board \
    test/storage_volume/gate_test.c test/storage_volume/os_cpu_fake.c \
    Drivers/storage_fatfs/fatfs/ff.c Drivers/storage_fatfs/fatfs/ffunicode.c -o "$build/gate-$transport"
  for scenario in "${scenarios[@]}"; do
    ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 timeout 20s "$build/gate-$transport" "$scenario"
  done
done
"${CC:-cc}" -std=c11 -D_XOPEN_SOURCE=700 -O1 -g -Wall -Wextra -Werror -Wno-overflow \
  -pthread -fsanitize=address,undefined -fno-omit-frame-pointer \
  -Itest/x4pro_sd_fake -Isdk/driver -IDrivers/x4pro_board \
  Drivers/x4pro_sd/driver.c test/x4pro_sd_absent_test.c test/storage_volume/os_cpu_fake.c \
  Drivers/storage_fatfs/fatfs/ff.c Drivers/storage_fatfs/fatfs/ffunicode.c -o "$build/absent"
ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 timeout 20s "$build/absent"
