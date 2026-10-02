#!/usr/bin/env python3
"""Embed the built X4 provider ELFs for SD-independent first boot."""
import hashlib
import json
import os
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "src/platform/x4pro_embedded.c"
PACKAGES = ["platform-clock-v1", "x4pro-panel", "x4pro-buttons", "x4pro-frontlight"]

def imports_for(elf):
    readelf = os.environ.get("XTENSA_READELF", "xtensa-esp32s3-elf-readelf")
    text = subprocess.check_output([readelf, "-sW", str(elf)], text=True)
    names = []
    for line in text.splitlines():
        if "UND" not in line:
            continue
        name = line.split()[-1]
        if name and name != "t5_driver_get" and name not in names:
            names.append(name)
    return names

def main():
    parts = ['#include "x4pro_embedded.h"\n']
    providers = []
    for package in PACKAGES:
        directory = ROOT / "dist/experimental" / package
        manifest = json.loads((directory / "manifest.json").read_text())
        payload = (directory / "driver.elf").read_bytes()
        digest = hashlib.sha256(payload).hexdigest()
        if manifest["sha256"] != digest:
            raise SystemExit(f"{package} hash mismatch")
        imports = imports_for(directory / "driver.elf")
        symbol = package.replace("-", "_")
        parts.append(f"static const uint8_t {symbol}_elf[] = {{")
        parts.append(",".join(str(b) for b in payload))
        parts.append("};\n")
        if imports:
            names = ",".join(f'"{name}"' for name in imports)
            parts.append(f"static const char *{symbol}_imports[] = {{{names}}};\n")
        providers.append((symbol, manifest, digest, imports))
    parts.append("static const x4_embedded_provider providers[] = {\n")
    for symbol, manifest, digest, imports in providers:
        sha = ",".join(f"0x{digest[i:i+2]}" for i in range(0, 64, 2))
        import_sym = f"{symbol}_imports" if imports else "0"
        parts.append(
            f'  {{"{manifest["id"]}", "{manifest["version"]}", "{manifest["provides"][0]["capability"]}", '
            f'{manifest["provides"][0]["api"]}, {symbol}_elf, sizeof({symbol}_elf), {{{sha}}}, '
            f'{import_sym}, {len(imports)}}},\n'
        )
    parts.append("};\n")
    parts.append("const x4_embedded_provider *x4_embedded_find(const char *id) {\n")
    parts.append("  for (size_t i = 0; i < sizeof(providers)/sizeof(providers[0]); ++i)\n")
    parts.append("    if (id && providers[i].id && !__builtin_strcmp(id, providers[i].id)) return &providers[i];\n")
    parts.append("  return 0;\n}\n")
    OUT.write_text("".join(parts))
    print(f"embedded {len(providers)} providers")

if __name__ == "__main__":
    main()
