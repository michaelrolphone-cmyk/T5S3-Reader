#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
sdk=${RISCRTE_PROVIDER_SDK_ROOT:-$root}
if [ ! -f "$sdk/sdk/driver/RiscStreamProviderV1.h" ]; then
  echo 'Requires pinned U1 provider SDK via RISCRTE_PROVIDER_SDK_ROOT' >&2; exit 1
fi
out=$(mktemp -d)
trap 'rm -rf "$out"' EXIT
${CC:-cc} -std=c11 -Wall -Wextra -Werror -Wno-missing-field-initializers \
  -I"$sdk/sdk/driver" -I"$root/sdk/driver" -I"$root/lib/NativeApps/include" \
  "$root/Drivers/camera_esp32s3/driver.c" "$root/test/camera/provider_test.c" -o "$out/test"
"$out/test"
