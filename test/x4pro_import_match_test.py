#!/usr/bin/env python3
"""Validate generated import metadata through the production exact-import matcher."""
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
from generate_privileged_imports_v1 import extract_imports as undefined_imports

PACKAGES = {
    "platform-clock-v1": ["clock_gettime", "usleep"],
    # GCC 14 -O2 emits the panel's byte clear inline; older -Os builds
    # synthesize memset. Both must still match their own exact ELF imports.
    "x4pro-panel": None,
    "x4pro-buttons": [],
    "x4pro-frontlight": [],
    "x4pro-i2c": sorted(["xPortInIsrContext", "xQueueCreateMutex", "xQueueGenericSend",
                           "xQueueSemaphoreTake", "xTaskGetCurrentTaskHandle", "vQueueDelete"]),
}

def main():
    harness = Path("/tmp/x4-import-match.c")
    harness.write_text(r'''
#include "private/esp_privileged_manifest_imports.h"
#include <stdio.h>
#include <stdlib.h>
int main(int argc, char **argv) {
    FILE *file = fopen(argv[1], "rb");
    fseek(file, 0, SEEK_END);
    long length = ftell(file);
    rewind(file);
    unsigned char *image = malloc(length);
    fread(image, 1, length, file);
    const char *declared[8] = {0};
    for (int i = 2; i < argc && i - 2 < 8; ++i) declared[i - 2] = argv[i];
    int ok = esp_elf_privileged_manifest_imports_match_v1(image, length, declared, argc - 2);
    return ok ? 0 : 1;
}
''')
    binary = Path("/tmp/x4-import-match")
    stub = Path("/tmp/x4-import-include")
    stub.mkdir(exist_ok=True)
    (stub / "sdkconfig.h").write_text("#pragma once\n")
    subprocess.run([
        "cc", "-std=c11", "-D_GNU_SOURCE", "-Wall", "-Wextra", "-Werror", "-I", str(stub),
        "-I", str(ROOT / "lib/elf_loader/include"),
        str(harness), str(ROOT / "lib/elf_loader/src/esp_privileged_manifest_imports.c"),
        str(ROOT / "lib/elf_loader/src/esp_privileged_imports.c"), "-o", str(binary)
    ], check=True)
    for package, expected in PACKAGES.items():
        elf = ROOT / "dist/experimental" / package / "driver.elf"
        found = undefined_imports(elf)
        if (package == "x4pro-panel" and found not in ([], ["memset"])) or \
                (package != "x4pro-panel" and found != expected):
            raise SystemExit(f"{package} imports {found} != {expected}")
        if "UND" in found:
            raise SystemExit(f"{package} kept the unnamed UND row")
        result = subprocess.run([str(binary), str(elf), *found])
        if result.returncode != 0:
            raise SystemExit(f"{package} exact import match failed")
        rejected = subprocess.run([str(binary), str(elf), *(found + ["not_a_real_import"])])
        if rejected.returncode == 0:
            raise SystemExit(f"{package} accepted an extra import")
        if found:
            missing = subprocess.run([str(binary), str(elf), *found[:-1]])
            if missing.returncode == 0:
                raise SystemExit(f"{package} accepted a missing import")
    print("x4 import match: PASS")

if __name__ == "__main__":
    main()
