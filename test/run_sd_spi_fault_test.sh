#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
build="$(mktemp -d)"
trap 'rm -rf "$build"' EXIT
c++ -std=c++17 -Wall -Wextra -Werror -DRISCRTE_SD_SPI_FAULT_TEST \
  -Itest/hal/spi_fault_stubs -Ilib/hal \
  lib/hal/SdSpiFault.cpp test/hal/sd_spi_fault_test.cpp -o "$build/policy"
for mode in normal busy lock operation wrap capacity delete-active wdt-failure; do "$build/policy" "$mode"; done
c++ -std=c++17 -Wall -Wextra -Werror -Wno-overloaded-virtual -DRISCRTE_SD_SPI_FAULT_TEST \
  -Itest/hal/storage_stubs -Ilib/hal -Isrc \
  lib/hal/HalStorage.cpp test/hal/storage_fault_retention_test.cpp -o "$build/storage"
"$build/storage"
c++ -std=c++17 -Wall -Wextra -Werror -DRISCRTE_SD_SPI_FAULT_TEST -DBOARD_T5S3_PRO \
  -Itest/hal/lora_fault_stubs -Ilib/hal -Ilib/NativeApps/include -Isrc \
  src/native/NativeLoRaBridge.cpp test/hal/lora_fault_retention_test.cpp -o "$build/lora"
"$build/lora"
for board in BOARD_T5S3_PRO BOARD_LILYGO_EPD47_S3; do
  c++ -std=c++17 -Wall -Wextra -Werror -DRISCRTE_SD_SPI_FAULT_PORT -D"$board" \
    -Itest/hal/gpio_fault_stubs -Ilib/hal \
    lib/hal/SdSpiFaultPins.cpp test/hal/sd_spi_pins_test.cpp -o "$build/pins"
  "$build/pins"
done
cc -std=c11 -Wall -Wextra -Werror -Ilib/NativeApps/include \
  Apps/lora.c test/native_apps/lora_test.c -o "$build/lora-app"
"$build/lora-app"
"$build/lora-app" blocked-display
python3 test/hal/sd_spi_lifetime_binding_test.py
