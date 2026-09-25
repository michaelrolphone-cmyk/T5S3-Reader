#!/usr/bin/env python3
"""Validate and atomically update the RiscRTE independent release index."""

from __future__ import annotations

import argparse
import json
import os
import re
import tempfile
from pathlib import Path
from typing import Any


REPOSITORY = "michaelrolphone-cmyk/T5S3-Reader"
VERSION_RE = re.compile(r"(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\Z")
ID_RE = re.compile(r"[a-z0-9][a-z0-9._-]{0,63}\Z")
ASSET_RE = re.compile(r"[A-Za-z0-9][A-Za-z0-9._+-]{0,159}\Z")
SHA256_RE = re.compile(r"[0-9a-f]{64}\Z")
MAX_INDEX_BYTES = 64 * 1024
MAX_ENTRIES = {"apps": 128, "drivers": 64}
DRIVER_FILES = {".package.json", "driver.elf", "provider-abi.v1", "privileged-imports.v1"}


def version_tuple(value: str) -> tuple[int, int, int]:
    match = VERSION_RE.fullmatch(value) if isinstance(value, str) else None
    if not match:
        raise ValueError(f"version must be numeric MAJOR.MINOR.PATCH: {value!r}")
    return tuple(int(part) for part in match.groups())


def validate_record(product: str, record: Any) -> dict[str, Any]:
    if product not in ("firmware", "apps", "drivers"):
        raise ValueError("product must be firmware, apps, or drivers")
    if not isinstance(record, dict):
        raise ValueError("record must be a JSON object")
    required = {"version", "tag", "asset", "url", "size", "sha256"}
    if product != "firmware":
        required.add("manifest")
    missing = required - record.keys()
    if missing:
        raise ValueError("record is missing: " + ", ".join(sorted(missing)))

    version = record["version"]
    version_tuple(version)
    expected_prefix = "firmware-" if product == "firmware" else (
        "app-" if product == "apps" else "driver-"
    )
    stable_id = None
    if product == "firmware":
        expected_tag = f"firmware-v{version}"
    else:
        stable_id = record.get("id")
        if not isinstance(stable_id, str) or not ID_RE.fullmatch(stable_id):
            raise ValueError("package record requires a safe stable id")
        manifest = record["manifest"]
        if not isinstance(manifest, dict):
            raise ValueError("manifest must be an object")
        if manifest.get("version") != version:
            raise ValueError("manifest version must match the release record")
        if product == "apps":
            filename = manifest.get("file_name")
            if not isinstance(filename, str) or not filename.endswith(".elf"):
                raise ValueError("app manifest must name its .elf file")
            manifest_id = filename[:-4]
        else:
            manifest_id = manifest.get("id")
        if manifest_id != stable_id:
            raise ValueError("manifest identity must match the release record")
        if product == "drivers":
            if (not isinstance(manifest.get("capability"), str) or not manifest["capability"] or
                    type(manifest.get("api")) is not int or manifest["api"] <= 0):
                raise ValueError("driver manifest requires a capability and positive API")
            files = manifest.get("files")
            if not isinstance(files, list) or len(files) != 4:
                raise ValueError("driver manifest must list its four package files")
            inventory = {}
            for item in files:
                if not isinstance(item, dict) or not isinstance(item.get("name"), str):
                    raise ValueError("driver package inventory is malformed")
                if item["name"] in inventory or type(item.get("size_bytes")) is not int or item["size_bytes"] <= 0:
                    raise ValueError("driver package inventory has duplicate or invalid files")
                if not isinstance(item.get("sha256"), str) or not SHA256_RE.fullmatch(item["sha256"]):
                    raise ValueError("driver package inventory requires SHA-256 digests")
                inventory[item["name"]] = item
            if set(inventory) != DRIVER_FILES:
                raise ValueError("driver package inventory does not match the installable package format")
        expected_tag = f"{expected_prefix}{stable_id}-v{version}"

    tag = record["tag"]
    if tag != expected_tag:
        raise ValueError(f"tag must be {expected_tag!r}")
    asset = record["asset"]
    if not isinstance(asset, str) or not ASSET_RE.fullmatch(asset) or ".." in asset:
        raise ValueError("asset must be a safe basename")
    expected_asset = "firmware-t5s3-pro.bin" if product == "firmware" else (
        f"{stable_id}.elf" if product == "apps" else f"{stable_id}--driver.elf"
    )
    if asset != expected_asset:
        raise ValueError(f"asset must be {expected_asset!r}")
    expected_url = f"https://github.com/{REPOSITORY}/releases/download/{tag}/{asset}"
    if record["url"] != expected_url:
        raise ValueError("url must point to the record's immutable GitHub release asset")
    size = record["size"]
    if type(size) is not int or size <= 0:
        raise ValueError("size must be a positive integer")
    digest = record["sha256"]
    if not isinstance(digest, str) or not SHA256_RE.fullmatch(digest):
        raise ValueError("sha256 must be 64 lowercase hexadecimal characters")

    clean = {key: value for key, value in record.items()}
    clean["kind"] = product[:-1] if product != "firmware" else "firmware"
    return clean


def validate_index_budget(index: dict[str, Any]) -> dict[str, Any]:
    for kind, maximum in MAX_ENTRIES.items():
        entries = index.get(kind)
        if not isinstance(entries, list) or len(entries) > maximum:
            raise ValueError(f"{kind} index exceeds its {maximum}-entry limit")
    encoded = json.dumps(index, separators=(",", ":"), sort_keys=True).encode("utf-8")
    if len(encoded) > MAX_INDEX_BYTES:
        raise ValueError(f"release index exceeds its {MAX_INDEX_BYTES}-byte limit")
    return index


def update_index(index: Any, product: str, record: Any) -> dict[str, Any]:
    if not isinstance(index, dict) or index.get("schema") != 1:
        raise ValueError("index must be an object with schema=1")
    normalized = validate_record(product, record)
    result = {"schema": 1, "firmware": index.get("firmware"), "apps": index.get("apps"),
              "drivers": index.get("drivers")}
    if result["apps"] is None:
        result["apps"] = []
    if result["drivers"] is None:
        result["drivers"] = []
    if not isinstance(result["apps"], list) or not isinstance(result["drivers"], list):
        raise ValueError("apps and drivers index entries must be arrays")

    if product == "firmware":
        old = result["firmware"]
        if old is not None:
            old_version = version_tuple(old.get("version"))
            new_version = version_tuple(normalized["version"])
            if new_version < old_version:
                raise ValueError("firmware index update would move backwards")
            if new_version == old_version and old != normalized:
                raise ValueError("firmware version already points to different content")
        result["firmware"] = normalized
        return validate_index_budget(result)

    key = "apps" if product == "apps" else "drivers"
    entries = result[key]
    current = {entry.get("id"): entry for entry in entries if isinstance(entry, dict)}
    if len(current) != len(entries):
        raise ValueError(f"{key} index contains malformed or duplicate entries")
    previous = current.get(normalized["id"])
    if previous:
        old_version = version_tuple(previous.get("version"))
        new_version = version_tuple(normalized["version"])
        if new_version < old_version:
            raise ValueError(f"{key} index update would move {normalized['id']} backwards")
        if new_version == old_version and previous != normalized:
            raise ValueError(f"{normalized['id']} version already points to different content")
    current[normalized["id"]] = normalized
    result[key] = [current[item] for item in sorted(current)]
    return validate_index_budget(result)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--index", type=Path, required=True)
    parser.add_argument("--record", type=Path, required=True)
    parser.add_argument("--product", choices=("firmware", "apps", "drivers"), required=True)
    args = parser.parse_args()

    index = json.loads(args.index.read_text(encoding="utf-8"))
    record = json.loads(args.record.read_text(encoding="utf-8"))
    updated = update_index(index, args.product, record)
    args.index.parent.mkdir(parents=True, exist_ok=True)
    fd, temporary = tempfile.mkstemp(prefix=args.index.name + ".", dir=args.index.parent)
    try:
        with os.fdopen(fd, "w", encoding="utf-8") as stream:
            json.dump(updated, stream, indent=2, sort_keys=True)
            stream.write("\n")
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temporary, args.index)
    except Exception:
        try:
            os.unlink(temporary)
        except FileNotFoundError:
            pass
        raise
    print(f"Updated {args.product} entry in {args.index}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
