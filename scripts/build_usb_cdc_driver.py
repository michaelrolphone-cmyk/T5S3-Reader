#!/usr/bin/env python3
"""Build the first installable USB class-provider ELF without changing GPS packaging."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import zipfile

from driver_package import read_json
from native_app_symbols import firmware_exports, validate_imports

ROOT = Path(__file__).resolve().parents[1]
CAPABILITY = "usb.class.cdc_acm"
DRIVER_ID = "usb-cdc-acm"

def build(output, cc=None):
    source = ROOT / "Drivers/usb_cdc"
    manifest = read_json((source / "manifest.json").read_bytes())
    expected = dict(type="driver", id=DRIVER_ID, driver_abi=1,
                    architecture="xtensa-esp32s3", file_name="driver.elf")
    for key, value in expected.items():
        if manifest.get(key) != value:
            raise ValueError(f"Invalid USB class driver manifest field {key}")
    if manifest.get("requires") != [{"capability": "kernel.usb.host", "api": 1}] or \
       manifest.get("provides") != [{"capability": CAPABILITY, "api": 1}]:
        raise ValueError("Invalid USB driver dependency/capability contract")
    version = manifest.get("version", "")
    if not isinstance(version, str) or not version or any(c not in "0123456789." for c in version):
        raise ValueError("Invalid USB driver version")
    cc = cc or os.environ.get("NATIVE_DRIVER_CC") or shutil.which("xtensa-esp32s3-elf-gcc")
    if not cc:
        core = Path(os.environ.get("PLATFORMIO_CORE_DIR", Path.home() / ".platformio"))
        cc = str(core / "packages/toolchain-xtensa-esp32s3/bin/xtensa-esp32s3-elf-gcc")
    output.mkdir(parents=True, exist_ok=True)
    elf = output / "driver.elf"
    subprocess.run([cc, "-std=c11", "-Os", "-fPIC", "-mtext-section-literals", "-mlongcalls",
                    "-fvisibility=hidden", "-nostdlib", "-nostartfiles", "-shared",
                    "-I" + str(ROOT / "sdk/driver"), "-Wl,--hash-style=sysv",
                    "-Wl,--exclude-libs,ALL", str(source / "driver.c"), "-lgcc", "-o", str(elf)], check=True)
    readelf = str(Path(cc).with_name(Path(cc).name.replace("gcc", "readelf")))
    symbols = subprocess.check_output([readelf, "--dyn-syms", "--wide", str(elf)], text=True)
    exported = {fields[7] for line in symbols.splitlines()
                if len(fields := line.split()) >= 8 and fields[4] == "GLOBAL"
                and fields[6] != "UND" and fields[3] == "FUNC"}
    if exported != {"t5_driver_get"}:
        raise ValueError("USB class driver must only export t5_driver_get")
    allowed = {symbol for symbol in firmware_exports(ROOT) if not symbol.startswith("t5_")}
    validate_imports(symbols, allowed)
    payload = elf.read_bytes()
    if not 52 <= len(payload) <= 256 * 1024 or payload[:7] != b"\x7fELF\x01\x01\x01" or \
       int.from_bytes(payload[16:18], "little") != 3 or int.from_bytes(payload[18:20], "little") != 94:
        raise ValueError("Invalid Xtensa shared-object payload")
    manifest.update(size_bytes=len(payload), sha256=hashlib.sha256(payload).hexdigest())
    text = json.dumps(manifest, indent=2) + "\n"
    (output / "manifest.json").write_text(text)
    stem = output.parent / f"{DRIVER_ID}-{version}.t5driver"
    package = stem.with_suffix(".t5driver.zip")
    with zipfile.ZipFile(package, "w", compression=zipfile.ZIP_DEFLATED) as archive:
        archive.writestr("manifest.json", text)
        archive.writestr("driver.elf", payload)
    (output.parent / f"{DRIVER_ID}-{version}.t5driver.json").write_text(text)
    (output.parent / f"{DRIVER_ID}-{version}.t5driver.elf").write_bytes(payload)
    print(f"Built {elf} ({len(payload)} bytes); package: {package}")
    return package

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output-dir", type=Path, default=ROOT / "dist/drivers/usb-cdc-acm")
    parser.add_argument("--cc")
    args = parser.parse_args()
    build(args.output_dir, args.cc)
