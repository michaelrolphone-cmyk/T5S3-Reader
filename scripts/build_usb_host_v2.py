#!/usr/bin/env python3
"""Build USB host arbitration/discovery ELF; NOT a physical USB controller."""
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
from native_app_symbols import firmware_exports, validate_imports

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "Drivers/usb_host_v2"
OUTPUT = ROOT / "dist/experimental/usb-host-v2"


def build(cc=None):
    manifest = json.loads((SOURCE / "manifest.json").read_text())
    required = {
        "type": "driver", "id": "usb-host-v2", "driver_abi": 2,
        "architecture": "xtensa-esp32s3", "file_name": "driver.elf",
        "requires": [{"capability": "usb.controller", "api": 1}],
        "provides": [{"capability": "usb.host", "api": 1}],
        "status": "experimental-unpublished",
    }
    if any(manifest.get(k) != v for k, v in required.items()):
        raise ValueError("Invalid experimental USB host manifest")
    cc = cc or os.environ.get("NATIVE_DRIVER_CC") or shutil.which("xtensa-esp32s3-elf-gcc")
    if not cc:
        core = Path(os.environ.get("PLATFORMIO_CORE_DIR", Path.home() / ".platformio"))
        cc = str(core / "packages/toolchain-xtensa-esp32s3/bin/xtensa-esp32s3-elf-gcc")
    OUTPUT.mkdir(parents=True, exist_ok=True)
    elf = OUTPUT / "driver.elf"
    subprocess.run([
        cc, "-std=c11", "-Os", "-fPIC", "-mtext-section-literals", "-mlongcalls",
        "-fvisibility=hidden", "-nostdlib", "-nostartfiles", "-shared",
        "-I" + str(ROOT / "sdk/driver"), "-Wl,--hash-style=sysv",
        "-Wl,--exclude-libs,ALL", str(SOURCE / "driver.c"), "-lgcc", "-o", str(elf),
    ], check=True)
    readelf = str(Path(cc).with_name(Path(cc).name.replace("gcc", "readelf")))
    symbols = subprocess.check_output([readelf, "--dyn-syms", "--wide", str(elf)], text=True)
    exports = {f[7] for line in symbols.splitlines()
               if len(f := line.split()) >= 8 and f[4] == "GLOBAL"
               and f[6] != "UND" and f[3] == "FUNC"}
    if exports != {"t5_driver_get"}:
        raise ValueError("USB host ELF must export only t5_driver_get")
    validate_imports(symbols, {s for s in firmware_exports(ROOT) if not s.startswith("t5_")})
    payload = elf.read_bytes()
    if not 52 <= len(payload) <= 256 * 1024 or payload[:7] != b"\x7fELF\x01\x01\x01" or \
       int.from_bytes(payload[16:18], "little") != 3 or \
       int.from_bytes(payload[18:20], "little") != 94:
        raise ValueError("Invalid Xtensa USB host ELF")
    manifest.update(size_bytes=len(payload), sha256=hashlib.sha256(payload).hexdigest())
    (OUTPUT / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    print(f"Unpublished USB host arbitration ELF built: {elf}; real usb.controller ELF still required")
    return elf


if __name__ == "__main__":
    build()
