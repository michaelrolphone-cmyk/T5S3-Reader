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

# Compile the actual opt-in deployment helper against fault-injectable storage
# and package-manager boundaries; no board or package mutations are performed.
for package in driver_cam_ov3660_profile_0_1_0 driver_camera_esp32s3_ov3660_0_1_7; do
  symbol="_binary_dist_packages_${package}_xtensa_esp32s3_rte_zip"
  cat >> "$out/archives.s" <<EOF
.text
.globl ${symbol}_start
${symbol}_start:
.byte 1,2,3,4
.globl ${symbol}_end
${symbol}_end:
.byte 0
EOF
done
${CXX:-c++} -std=c++17 -Wall -Wextra -Werror -Wno-missing-field-initializers \
  -I"$root/test/camera/install_stubs" "$root/test/camera/install_test.cpp" \
  "$out/archives.s" -o "$out/install-test"
"$out/install-test"
