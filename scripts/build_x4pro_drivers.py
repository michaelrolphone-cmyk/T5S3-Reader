#!/usr/bin/env python3
"""Build Xteink X4 Pro capability drivers as Xtensa provider-v2 ELFs."""
import hashlib
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
from normalize_xtensa_relocations import normalize

def compiler():
    env = os.environ.get("NATIVE_DRIVER_CC")
    if env:
        return env
    found = shutil.which("xtensa-esp32s3-elf-gcc")
    if found:
        return found
    core = Path(os.environ.get("PLATFORMIO_CORE_DIR", Path.home() / ".platformio"))
    return str(core / "packages/toolchain-xtensa-esp32s3/bin/xtensa-esp32s3-elf-gcc")

CC = compiler()
PACKAGES = [
    "x4pro_i2c", "x4pro_panel", "x4pro_gt911", "x4pro_buttons",
    "x4pro_frontlight", "x4pro_battery", "x4pro_sd",
]

def build_one(name):
    source = ROOT / "Drivers" / name
    manifest = json.loads((source / "manifest.json").read_text())
    output = ROOT / "dist" / "experimental" / manifest["id"]
    output.mkdir(parents=True, exist_ok=True)
    elf = output / "driver.elf"
    # CI run 37048644826 confirmed that pinned Linux Xtensa ld 2.35.1 links
    # the enlarged FAT32 provider at -O2 with relaxation disabled. Keep the
    # workaround scoped; all other providers retain their ordinary flags.
    optimization = "-O2" if name == "x4pro_sd" else "-Os"
    link_flags = ["-Wl,--no-relax"] if name == "x4pro_sd" else []
    subprocess.run([
        CC, "-std=c11", optimization,
        "-fPIC", "-mtext-section-literals", "-mlongcalls",
        "-fvisibility=hidden", "-fno-builtin", "-nostdlib", "-nostartfiles", "-shared",
        "-I" + str(ROOT / "sdk/driver"), "-I" + str(ROOT / "Drivers/x4pro_board"),
        "-Wl,--hash-style=sysv", "-Wl,--exclude-libs,ALL", *link_flags,
        str(source / "driver.c"),
        *([str(source / "fatfs/ff.c"), str(source / "fatfs/ffunicode.c")] if name == "x4pro_sd" else []),
        "-lgcc", "-o", str(elf),
    ], check=True)
    readelf = CC.replace("gcc", "readelf")
    sections = subprocess.check_output([readelf, "-S", str(elf)], text=True)
    if ".rela.plt" in sections or ".rela.dyn" in sections:
        try:
            normalize(elf)
        except ValueError as exc:
            if "Missing required section" not in str(exc):
                raise
    symbols = subprocess.check_output([readelf, "--dyn-syms", "--wide", str(elf)], text=True)
    exported = {fields[7] for line in symbols.splitlines()
                if len(fields := line.split()) >= 8 and fields[4] == "GLOBAL"
                and fields[6] != "UND" and fields[3] == "FUNC"}
    if exported != {"t5_driver_get"}:
        raise ValueError(f"{name} exports {exported}")
    payload = elf.read_bytes()
    if payload[:7] != b"\x7fELF\x01\x01\x01" or int.from_bytes(payload[18:20], "little") != 94:
        raise ValueError(f"{name} is not an Xtensa shared object")
    manifest.update(size_bytes=len(payload), sha256=hashlib.sha256(payload).hexdigest())
    (output / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    print(f"{manifest['id']} {len(payload)} {manifest['sha256']}")

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", choices=PACKAGES,
                        help="build only one source directory for a release plan")
    args = parser.parse_args()
    for name in ([args.source] if args.source else PACKAGES):
        build_one(name)

if __name__ == "__main__":
    main()
