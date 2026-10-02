#!/usr/bin/env python3
"""Embed the built X4 provider ELFs for SD-independent first boot."""
import hashlib
import json
import os
import shutil
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "src/platform/x4pro_embedded.c"
PACKAGES = ["platform-clock-v1", "x4pro-panel", "x4pro-buttons", "x4pro-frontlight", "x4pro-sd"]

def undefined_imports(elf):
    """Named undefined symbols only. Skip the unnamed null symbol at index 0."""
    data = Path(elf).read_bytes()
    if data[:4] != b"\x7fELF" or data[4] != 1 or data[5] != 1:
        raise SystemExit(f"{elf} is not a little-endian ELF32")
    import struct
    e_shoff, e_shentsize, e_shnum, e_shstrndx = struct.unpack_from("<IIII", data, 32)
    e_shentsize = struct.unpack_from("<H", data, 46)[0]
    e_shnum = struct.unpack_from("<H", data, 48)[0]
    e_shstrndx = struct.unpack_from("<H", data, 50)[0]
    sections = []
    for i in range(e_shnum):
        off = e_shoff + i * e_shentsize
        sh_name, sh_type, sh_flags, sh_addr, sh_offset, sh_size, sh_link = struct.unpack_from("<IIIIIII", data, off)
        sections.append((sh_type, sh_offset, sh_size, sh_link))
    names = set()
    for sh_type, sh_offset, sh_size, sh_link in sections:
        if sh_type not in (2, 11):  # SHT_SYMTAB, SHT_DYNSYM
            continue
        str_off = sections[sh_link][1]
        count = sh_size // 16
        for index in range(count):
            st_name, st_value, st_size, st_info, st_other, st_shndx = struct.unpack_from("<IIIBBH", data, sh_offset + index * 16)
            if st_shndx != 0:
                continue
            if index == 0 and st_name == 0 and st_info == 0:
                continue
            end = data.index(b"\0", str_off + st_name)
            name = data[str_off + st_name:end].decode("ascii")
            if name:
                names.add(name)
    return sorted(names)

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
        imports = undefined_imports(directory / "driver.elf")
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
