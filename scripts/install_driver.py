#!/usr/bin/env python3
"""Validate and install a GPS driver package onto a mounted SD card."""
import argparse
import os
from pathlib import Path
import shutil
import tempfile
import zipfile
from driver_package import MAX_ELF_BYTES, read_json, validate_payload

def install(package, sd_root):
    if not sd_root.is_dir():
        raise ValueError("--sd-root must be an existing mounted directory")
    with zipfile.ZipFile(package) as archive:
        entries = archive.infolist()
        if len(entries) != 2 or {entry.filename for entry in entries} != {"manifest.json", "driver.elf"}:
            raise ValueError("Package must contain exactly manifest.json and driver.elf")
        for entry in entries:
            limit = 4096 if entry.filename == "manifest.json" else MAX_ELF_BYTES
            if entry.file_size > limit:
                raise ValueError("Oversized package entry")
        manifest_bytes = archive.read("manifest.json")
        payload = archive.read("driver.elf")
    manifest = validate_payload(read_json(manifest_bytes), payload)
    drivers = sd_root / "Drivers"
    if drivers.is_symlink():
        raise ValueError("Drivers directory must not be a symlink")
    drivers.mkdir(exist_ok=True)
    target = drivers / manifest["id"]
    backup = drivers / ("." + manifest["id"] + ".previous")
    if target.is_symlink() or backup.exists():
        raise ValueError("Resolve the existing backup/symlink before updating this driver")
    if target.exists() and (not target.is_dir() or {p.name for p in target.iterdir()} != {"manifest.json", "driver.elf"}):
        raise ValueError("Existing driver directory contains unmanaged files")
    stage = Path(tempfile.mkdtemp(prefix=".gps-nmea-", dir=drivers))
    try:
        for name, content in (("driver.elf", payload), ("manifest.json", manifest_bytes)):
            with (stage / name).open("wb") as stream:
                stream.write(content)
                stream.flush()
                os.fsync(stream.fileno())
        if target.exists():
            target.rename(backup)
        try:
            stage.rename(target)
        except OSError:
            if backup.exists(): backup.rename(target)
            raise
        # Retain the previous complete package for explicit rollback.
    finally:
        if stage.exists(): shutil.rmtree(stage)
    print(f"Installed {manifest['id']} {manifest['version']} at {target}")
    if backup.exists(): print(f"Previous package retained at {backup}; remove it after verifying the update")
    return target

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("package", type=Path)
    parser.add_argument("--sd-root", type=Path, required=True)
    args = parser.parse_args()
    install(args.package, args.sd_root)
