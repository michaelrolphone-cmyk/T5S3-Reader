#!/usr/bin/env python3
"""Pack one ordinary package directory into a stored .rte.zip.

Reads <source>/.package.json and every declared entry. Writes a deterministic
ZIP (method 0, zero timestamps, sorted by declared order then manifest first).
Does not sign, does not consult usb-provider-catalog.json.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import struct
import sys
import zlib
from pathlib import Path


def crc32(data: bytes) -> int:
    return zlib.crc32(data) & 0xFFFFFFFF


def put_local(name: bytes, payload: bytes, crc: int) -> bytes:
    return struct.pack(
        "<IHHHHHIIIHH",
        0x04034B50,
        20,
        0,
        0,
        0,
        0,
        crc,
        len(payload),
        len(payload),
        len(name),
        0,
    ) + name + payload


def put_central(name: bytes, payload: bytes, crc: int, offset: int) -> bytes:
    return struct.pack(
        "<IHHHHHHIIIHHHHHII",
        0x02014B50,
        20,
        20,
        0,
        0,
        0,
        0,
        crc,
        len(payload),
        len(payload),
        len(name),
        0,
        0,
        0,
        0,
        0,
        offset,
    ) + name


def pack_directory(source: Path) -> bytes:
    manifest_path = source / ".package.json"
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    if manifest.get("schema") != 1:
        raise ValueError("ordinary schema 1 required")
    files = [(".package.json", manifest_path.read_bytes())]
    declared = set()
    for entry in manifest["entries"]:
        name = entry["name"]
        if name in declared or name == ".package.json":
            raise ValueError(f"invalid entry name: {name}")
        payload = (source / name).read_bytes()
        if len(payload) != int(entry["size_bytes"]):
            raise ValueError(f"size mismatch: {name}")
        digest = hashlib.sha256(payload).hexdigest()
        if digest != entry["sha256"]:
            raise ValueError(f"sha256 mismatch: {name}")
        files.append((name, payload))
        declared.add(name)
    locals_ = bytearray()
    centrals = bytearray()
    offset = 0
    for name, payload in files:
        encoded = name.encode("ascii")
        crc = crc32(payload)
        local = put_local(encoded, payload, crc)
        centrals.extend(put_central(encoded, payload, crc, offset))
        locals_.extend(local)
        offset += len(local)
    eocd = struct.pack(
        "<IHHHHIIH",
        0x06054B50,
        0,
        0,
        len(files),
        len(files),
        len(centrals),
        len(locals_),
        0,
    )
    return bytes(locals_) + bytes(centrals) + eocd


def catalog_row(source: Path, archive_name: str, archive: bytes) -> dict:
    manifest = json.loads((source / ".package.json").read_text(encoding="utf-8"))
    return {
        "kind": manifest["kind"],
        "id": manifest["id"],
        "version": manifest["version"],
        "artifact": manifest["artifact"],
        "architecture": manifest["architecture"],
        "archive": archive_name,
        "size_bytes": len(archive),
        "sha256": hashlib.sha256(archive).hexdigest(),
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path, help="ordinary package directory")
    parser.add_argument("output", type=Path, help="destination .rte.zip")
    args = parser.parse_args()
    archive = pack_directory(args.source)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(archive)
    print(json.dumps(catalog_row(args.source, args.output.name, archive)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
