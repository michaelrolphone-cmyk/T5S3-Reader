#!/usr/bin/env python3
"""Build the EPD display.output provider as an independent provider-v2 ELF."""
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess

from native_app_symbols import firmware_exports, validate_imports
from normalize_xtensa_relocations import normalize

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "Drivers/display_epd_video"
OUTPUT = ROOT / "dist/experimental/display-epd-video"


def build(cc=None):
    manifest = json.loads((SOURCE / "manifest.json").read_text())
    required = {
        "type": "driver",
        "id": "display-epd-video",
        "driver_abi": 2,
        "architecture": "xtensa-esp32s3",
        "file_name": "driver.elf",
        "requires": [],
        "provides": [{"capability": "display.output", "api": 1}],
        "status": "experimental-unpublished",
    }
    if any(manifest.get(key) != value for key, value in required.items()):
        raise ValueError("Invalid EPD display provider manifest")

    cc = cc or os.environ.get("NATIVE_DRIVER_CC") or shutil.which("xtensa-esp32s3-elf-gcc")
    if not cc:
        core = Path(os.environ.get("PLATFORMIO_CORE_DIR", Path.home() / ".platformio"))
        cc = str(core / "packages/toolchain-xtensa-esp32s3/bin/xtensa-esp32s3-elf-gcc")

    OUTPUT.mkdir(parents=True, exist_ok=True)
    elf = OUTPUT / "driver.elf"
    subprocess.run([
        cc, "-std=c11", "-Os", "-fPIC", "-mtext-section-literals", "-mlongcalls",
        "-fvisibility=hidden", "-nostdlib", "-nostartfiles", "-shared",
        "-I" + str(ROOT / "sdk/driver"),
        "-I" + str(ROOT / "lib/NativeApps/include"),
        "-Wl,--hash-style=sysv", "-Wl,--exclude-libs,ALL",
        str(SOURCE / "driver.c"), "-lgcc", "-o", str(elf),
    ], check=True)
    normalize(elf)

    readelf = str(Path(cc).with_name(Path(cc).name.replace("gcc", "readelf")))
    symbols = subprocess.check_output([readelf, "--dyn-syms", "--wide", str(elf)], text=True)
    exported = {
        fields[7] for line in symbols.splitlines()
        if len(fields := line.split()) >= 8 and fields[4] == "GLOBAL"
        and fields[6] != "UND" and fields[3] == "FUNC"
    }
    if exported != {"t5_driver_get"}:
        raise ValueError("Display provider must export only t5_driver_get: " + repr(exported))
    # The provisional physical backend is the single private board video
    # bridge. Apps import only display.output, and no other t5_ bridge is allowed.
    validate_imports(
        symbols,
        {name for name in firmware_exports(ROOT) if not name.startswith("t5_")}
        | {"t5_video_get_api"},
    )

    payload = elf.read_bytes()
    if (
        not 52 <= len(payload)
        or payload[:7] != b"\x7fELF\x01\x01\x01"
        or int.from_bytes(payload[16:18], "little") != 3
        or int.from_bytes(payload[18:20], "little") != 94
    ):
        raise ValueError("Invalid Xtensa display provider ELF")

    manifest.update(size_bytes=len(payload), sha256=hashlib.sha256(payload).hexdigest())
    (OUTPUT / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    print("Installable display.output EPD provider built")
    return elf


if __name__ == "__main__":
    build()
