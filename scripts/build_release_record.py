#!/usr/bin/env python3
"""Create a verified release-index record from built product artifacts."""
from __future__ import annotations

import argparse
import hashlib
import json
import re
from pathlib import Path

REPOSITORY = "michaelrolphone-cmyk/T5S3-Reader"
VERSION_RE = re.compile(r"(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\Z")


def digest(path: Path) -> tuple[int, str]:
    data = path.read_bytes()
    if not data:
        raise ValueError(f"release asset is empty: {path}")
    return len(data), hashlib.sha256(data).hexdigest()


def build_record(product: str, identity: str, version: str, root: Path) -> dict:
    if not VERSION_RE.fullmatch(version):
        raise ValueError("version must be numeric MAJOR.MINOR.PATCH")
    prefix = {"firmware": "firmware", "apps": "app", "drivers": "driver"}[product]
    tag = f"{prefix}-v{version}" if product == "firmware" else f"{prefix}-{identity}-v{version}"
    if product == "firmware":
        asset_path = root / "dist/firmware-t5s3-pro.bin"
        manifest = None
    elif product == "apps":
        manifest_path = root / "dist/apps" / f"{identity}.json"
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
        if manifest.get("file_name") != f"{identity}.elf" or manifest.get("version") != version:
            raise ValueError("built app manifest identity/version does not match the request")
        asset_path = root / "dist/apps" / f"{identity}.elf"
    else:
        catalog_path = root / "dist/packages/usb-provider-catalog.json"
        catalog = json.loads(catalog_path.read_text(encoding="utf-8"))
        packages = catalog.get("packages")
        if catalog.get("schema") != 1 or not isinstance(packages, list):
            raise ValueError("built canonical driver catalog is missing or malformed")
        matches = [item for item in packages if isinstance(item, dict) and item.get("id") == identity]
        if len(matches) != 1:
            raise ValueError(f"expected one built package for driver {identity!r}")
        manifest = matches[0]
        if manifest.get("version") != version:
            raise ValueError("built driver manifest identity/version does not match the request")
        asset_path = root / "dist/release-packages" / f"{identity}--driver.elf"
        inventory = manifest.get("files")
        if not isinstance(inventory, list) or not inventory:
            raise ValueError("driver release manifest must include its complete file inventory")
        declared = {entry.get("name"): entry for entry in inventory if isinstance(entry, dict)}
        if len(declared) != len(inventory) or "driver.elf" not in declared:
            raise ValueError("driver package inventory is malformed or omits driver.elf")
        size, sha256 = digest(asset_path)
        elf = declared["driver.elf"]
        if elf.get("size_bytes") != size or elf.get("sha256") != sha256:
            raise ValueError("driver ELF does not match the package inventory")
    size, sha256 = digest(asset_path)
    asset = asset_path.name
    record = {
        "id": identity if product != "firmware" else None,
        "version": version,
        "tag": tag,
        "asset": asset,
        "url": f"https://github.com/{REPOSITORY}/releases/download/{tag}/{asset}",
        "size": size,
        "sha256": sha256,
    }
    if product != "firmware":
        record["manifest"] = manifest
    else:
        record.pop("id")
    return record


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--product", required=True, choices=("firmware", "apps", "drivers"))
    parser.add_argument("--id", default="")
    parser.add_argument("--version", required=True)
    parser.add_argument("--root", type=Path, default=Path("."))
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    if args.product != "firmware" and not args.id:
        parser.error("--id is required for apps and drivers")
    record = build_record(args.product, args.id, args.version, args.root)
    args.output.write_text(json.dumps(record, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print(f"Verified {args.product} release record for {args.id or 'firmware'}")


if __name__ == "__main__":
    main()
