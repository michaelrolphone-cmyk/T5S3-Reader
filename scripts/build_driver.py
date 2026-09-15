#!/usr/bin/env python3
"""Build GPS driver independently of firmware and package an installable ZIP."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import zipfile
from driver_package import read_json, validate_manifest, validate_payload
from native_app_symbols import firmware_exports, validate_imports

ROOT = Path(__file__).resolve().parents[1]

def build(output, cc=None):
    source = ROOT / "Drivers/gps_nmea"
    manifest = validate_manifest(read_json((source / "manifest.json").read_bytes()))
    cc = cc or os.environ.get("NATIVE_DRIVER_CC") or shutil.which("xtensa-esp32s3-elf-gcc")
    if not cc:
        core = Path(os.environ.get("PLATFORMIO_CORE_DIR", Path.home() / ".platformio"))
        cc = str(core / "packages/toolchain-xtensa-esp32s3/bin/xtensa-esp32s3-elf-gcc")
    output.mkdir(parents=True, exist_ok=True)
    elf = output / "driver.elf"
    subprocess.run([cc, "-std=c11", "-Os", "-fPIC", "-mtext-section-literals", "-mlongcalls",
                    "-fvisibility=hidden", "-nostdlib", "-nostartfiles", "-shared",
                    "-I" + str(ROOT / "sdk/driver"), "-I" + str(ROOT / "lib/NativeApps/include"),
                    "-Wl,--hash-style=sysv", "-Wl,--exclude-libs,ALL", str(source / "driver.c"), "-lgcc", "-o", str(elf)], check=True)
    readelf = str(Path(cc).with_name(Path(cc).name.replace("gcc", "readelf")))
    symbols = subprocess.check_output([readelf, "--dyn-syms", "--wide", str(elf)], text=True)
    exported = {f[7] for line in symbols.splitlines() if len(f := line.split()) >= 8
                and f[4] == "GLOBAL" and f[6] != "UND" and f[3] == "FUNC"}
    if exported != {"t5_driver_get"}:
        raise ValueError("Driver must export t5_driver_get and must not export app_main")
    # No app API or globally exported kernel symbols: primitives are injected.
    allowed = firmware_exports(ROOT)
    allowed = {name for name in allowed if not name.startswith("t5_")}
    validate_imports(symbols, allowed)
    payload = elf.read_bytes()
    manifest.update(size_bytes=len(payload), sha256=hashlib.sha256(payload).hexdigest())
    validate_payload(manifest, payload)
    text = json.dumps(manifest, indent=2) + "\n"
    (output / "manifest.json").write_text(text)
    package = output.parent / f"gps-nmea-{manifest['version']}.t5driver.zip"
    with zipfile.ZipFile(package, "w", compression=zipfile.ZIP_DEFLATED) as archive:
        archive.writestr("manifest.json", text)
        archive.writestr("driver.elf", payload)
    print(f"Built {elf} ({len(payload)} bytes)")
    print(f"Installable package: {package}")
    return package

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output-dir", type=Path, default=ROOT / "dist/drivers/gps-nmea")
    parser.add_argument("--cc")
    args = parser.parse_args()
    build(args.output_dir, args.cc)
