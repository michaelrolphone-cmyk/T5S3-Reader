#!/usr/bin/env bash
set -euo pipefail
repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build="$(mktemp -d)"
trap 'rm -rf "$build"' EXIT
san=(-fsanitize=address,undefined -fno-omit-frame-pointer)
c++ -std=c++17 -Wall -Wextra -Werror -shared -fPIC -fvisibility=hidden "${san[@]}" \
  -I"$repo/sdk/driver" -I"$repo/src" "$repo/Services/archive_zip/service.cpp" -o "$build/archive.so"
cc -std=c11 -D_DEFAULT_SOURCE -Wall -Wextra -Werror -shared -fPIC -fvisibility=hidden "${san[@]}" \
  -I"$repo/sdk/driver" "$repo/Drivers/platform_clock_v1/driver.c" -o "$build/clock.so"
python3 - "$build" <<'PY'
from pathlib import Path
import sys, zipfile
root=Path(sys.argv[1])
with zipfile.ZipFile(root/'good.zip','w',compression=zipfile.ZIP_STORED) as z:
    z.writestr('assets/text.txt','abc');z.writestr('empty.txt','')
with zipfile.ZipFile(root/'empty.zip','w'):pass
with zipfile.ZipFile(root/'large.zip','w') as z:z.writestr('large.bin','x'*120000)
with zipfile.ZipFile(root/'path.zip','w') as z:z.writestr('../outside.txt','abc')
with zipfile.ZipFile(root/'method.zip','w',compression=zipfile.ZIP_DEFLATED) as z:z.writestr('text.txt','abc')
data=bytearray((root/'good.zip').read_bytes());data[data.index(b'abc')]=ord('z');(root/'crc.zip').write_bytes(data)
with zipfile.ZipFile(root/'link.zip','w') as z:
    i=zipfile.ZipInfo('link');i.create_system=3;i.external_attr=(0o120777<<16);z.writestr(i,'target')
PY
c++ -std=c++17 -Wall -Wextra -Werror "${san[@]}" -I"$repo/sdk/driver" -I"$repo/src" -I"$repo/test/drivers/stubs" \
  "$repo/src/runtime/drivers/ProviderGraphV2.cpp" "$repo/src/runtime/drivers/ProviderModuleV2.cpp" \
  "$repo/test/services/archive_zip_test.cpp" -ldl -o "$build/test"
"$build/test" "$build/archive.so" "$build/clock.so" "$build/good.zip" "$build/empty.zip" \
  "$build/path.zip" "$build/method.zip" "$build/crc.zip" "$build/link.zip" "$build/large.zip"
