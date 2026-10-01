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
TEMP_GAMEBOY_REPOSITORY = "michaelrolphone-cmyk/T5S3-GameBoy"
GAMEBOY_TAG_RE = re.compile(r"v(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\Z")
VERSION_RE = re.compile(r"(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\Z")
ID_RE = re.compile(r"[a-z0-9][a-z0-9._-]{0,63}\Z")
ASSET_RE = re.compile(r"[A-Za-z0-9][A-Za-z0-9._+-]{0,159}\Z")
SHA256_RE = re.compile(r"[0-9a-f]{64}\Z")
MAX_ENTRIES = {"apps": 128, "drivers": 64}
DRIVER_FILES = {".package.json", "driver.elf", "provider-abi.v1", "privileged-imports.v1"}
BUNDLE_FORMAT = "rte.zip"
BUNDLE_MANIFEST_KEYS = {"schema", "kind", "id", "version", "artifact", "architecture",
                        "min_runtime_api", "entries", "requires"}
BUNDLE_ENTRY_KEYS = {"name", "size_bytes", "sha256", "executable"}
BUNDLE_REQUIREMENT_KEYS = {"capability", "min_api"}
BUNDLE_ARCHITECTURES = {"xtensa-esp32s3", "riscv32"}
BUNDLE_ID_RE = re.compile(r"[a-z0-9](?:[a-z0-9_-]*[a-z0-9])?\Z")
BUNDLE_NAME_RE = re.compile(r"[a-z0-9](?:[a-z0-9._-]*[a-z0-9])?\Z")
# PackageOrdinaryManifest/Preflight and the narrower current stored-ZIP
# bootstrap bounds. Historical independent records keep their old contract.
BUNDLE_MAX_ENTRIES = 16
BUNDLE_MAX_REQUIREMENTS = 16
BUNDLE_MAX_ENTRY_BYTES = 1024 * 1024
BUNDLE_MAX_TOTAL_BYTES = 4 * 1024 * 1024
BUNDLE_MAX_MANIFEST_BYTES = 4096
BUNDLE_MAX_ARCHIVE_BYTES = BUNDLE_MAX_TOTAL_BYTES + 65536
UINT32_MAX = 0xFFFFFFFF


def version_tuple(value: str) -> tuple[int, int, int]:
    match = VERSION_RE.fullmatch(value) if isinstance(value, str) else None
    if not match:
        raise ValueError(f"version must be numeric MAJOR.MINOR.PATCH: {value!r}")
    return tuple(int(part) for part in match.groups())


def validate_bundle_manifest(manifest: Any, identity: str, version: str,
                             architecture: str, kind: str = "driver") -> dict[str, Any]:
    """Validate the exact ordinary driver descriptor, without granting trust.

    This structural check is shared by record construction and index intake.
    The builder must additionally verify raw manifest length, all payload
    hashes/ELF bytes and the actual ZIP; an index alone cannot prove those.
    """
    if not isinstance(manifest, dict) or set(manifest) != BUNDLE_MANIFEST_KEYS:
        raise ValueError("bundle manifest requires exactly the ordinary schema-1 fields")
    if type(manifest["schema"]) is not int or manifest["schema"] != 1:
        raise ValueError("bundle manifest requires ordinary schema 1")
    if (kind not in ("driver", "application") or manifest["kind"] != kind or manifest["id"] != identity or
            manifest["version"] != version or manifest["architecture"] != architecture):
        raise ValueError("bundle manifest kind, identity, version and architecture must match the record")
    if (not isinstance(identity, str) or not 0 < len(identity) < 64 or
            not BUNDLE_ID_RE.fullmatch(identity)):
        raise ValueError("bundle manifest requires a canonical package identity")
    if (not isinstance(version, str) or len(version) >= 32 or
            any(component > UINT32_MAX for component in version_tuple(version))):
        raise ValueError("bundle manifest version must contain canonical uint32 components")
    if not isinstance(architecture, str) or architecture not in BUNDLE_ARCHITECTURES:
        raise ValueError("bundle manifest architecture is unsupported")
    artifact = manifest["artifact"]
    if (not isinstance(artifact, str) or not 4 < len(artifact) < 128 or
            not BUNDLE_NAME_RE.fullmatch(artifact) or ".." in artifact or
            not artifact.endswith(".elf")):
        raise ValueError("bundle manifest requires a safe executable artifact")
    if (type(manifest["min_runtime_api"]) is not int or
            not 0 < manifest["min_runtime_api"] <= UINT32_MAX):
        raise ValueError("bundle manifest requires a positive uint32 runtime API")

    entries = manifest["entries"]
    if not isinstance(entries, list) or not 1 <= len(entries) <= BUNDLE_MAX_ENTRIES:
        raise ValueError("bundle manifest inventory must contain 1-16 entries")
    inventory = {}
    total = 0
    for item in entries:
        if not isinstance(item, dict) or set(item) != BUNDLE_ENTRY_KEYS:
            raise ValueError("bundle inventory requires exact ordinary entry fields")
        name = item["name"]
        if (not isinstance(name, str) or not 0 < len(name) < 128 or
                not BUNDLE_NAME_RE.fullmatch(name) or ".." in name or
                name.casefold() in inventory):
            raise ValueError("bundle inventory has an unsafe or duplicate entry name")
        if (type(item["size_bytes"]) is not int or
                not 0 < item["size_bytes"] <= BUNDLE_MAX_ENTRY_BYTES):
            raise ValueError("bundle inventory entry exceeds the ZIP size bound")
        if not isinstance(item["sha256"], str) or not SHA256_RE.fullmatch(item["sha256"]):
            raise ValueError("bundle inventory requires lowercase SHA-256 digests")
        if type(item["executable"]) is not bool:
            raise ValueError("bundle inventory executable role must be boolean")
        if (item["executable"] != (name == artifact) or
                (name.endswith(".elf") and name != artifact) or
                (item["executable"] and item["size_bytes"] < 52)):
            raise ValueError("bundle inventory must contain exactly its declared executable")
        inventory[name.casefold()] = item
        total += item["size_bytes"]
    required_entries = {"provider-abi.v1", "privileged-imports.v1"} if kind == "driver" else {f"{identity}.json"}
    if artifact not in inventory or not required_entries <= inventory.keys():
        raise ValueError("bundle omits its executable or required runtime metadata")
    if kind == "application" and artifact != f"{identity}.elf":
        raise ValueError("application bundle must retain its canonical executable name")

    requirements = manifest["requires"]
    if not isinstance(requirements, list) or len(requirements) > BUNDLE_MAX_REQUIREMENTS:
        raise ValueError("bundle manifest exceeds its 16-requirement bound")
    capabilities = set()
    for requirement in requirements:
        if not isinstance(requirement, dict) or set(requirement) != BUNDLE_REQUIREMENT_KEYS:
            raise ValueError("bundle dependency requires exact capability/min_api fields")
        capability = requirement["capability"]
        if (not isinstance(capability, str) or not 0 < len(capability) < 64 or
                not BUNDLE_NAME_RE.fullmatch(capability) or ".." in capability or
                capability in capabilities or type(requirement["min_api"]) is not int or
                not 0 < requirement["min_api"] <= UINT32_MAX):
            raise ValueError("bundle dependency has an unsafe, duplicate or invalid capability/API")
        capabilities.add(capability)

    # Values have now been reduced to bounded ASCII strings, exact scalar
    # types and bounded lists. Include the manifest in the ZIP content budget.
    encoded_size = len(json.dumps(manifest, separators=(",", ":"), ensure_ascii=True))
    if encoded_size > BUNDLE_MAX_MANIFEST_BYTES:
        raise ValueError("bundle manifest exceeds its 4096-byte parser bound")
    if total + encoded_size > BUNDLE_MAX_TOTAL_BYTES:
        raise ValueError("bundle inventory exceeds the ZIP total-content bound")
    return manifest


def validate_record(product: str, record: Any) -> dict[str, Any]:
    if product not in ("firmware", "apps", "drivers"):
        raise ValueError("product must be firmware, apps, or drivers")
    if not isinstance(record, dict):
        raise ValueError("record must be a JSON object")
    kind = product[:-1] if product != "firmware" else "firmware"
    if "kind" in record and record["kind"] != kind:
        raise ValueError("record kind must match its product")
    bundled = "format" in record
    if bundled and (product not in ("apps", "drivers") or record["format"] != BUNDLE_FORMAT):
        raise ValueError("only app/driver records support explicit format rte.zip")
    if not bundled and "architecture" in record:
        raise ValueError("archive architecture requires explicit format rte.zip")
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
        if product == "apps" and not bundled:
            filename = manifest.get("file_name")
            if not isinstance(filename, str) or not filename.endswith(".elf"):
                raise ValueError("app manifest must name its .elf file")
            manifest_id = filename[:-4]
        else:
            manifest_id = manifest.get("id")
        if manifest_id != stable_id:
            raise ValueError("manifest identity must match the release record")
        if bundled:
            validate_bundle_manifest(manifest, stable_id, version, record.get("architecture"),
                                     "application" if product == "apps" else "driver")
        elif product == "drivers":
            validate_legacy_driver_manifest(manifest)
        source_repo = record.get("source_repo")
        if source_repo is not None:
            if (bundled or product != "apps" or stable_id != "gameboy" or
                    source_repo != TEMP_GAMEBOY_REPOSITORY):
                raise ValueError("source_repo is reserved for the temporary GameBoy app provider")
            expected_tag = record.get("tag")
            if not isinstance(expected_tag, str) or not GAMEBOY_TAG_RE.fullmatch(expected_tag):
                raise ValueError("GameBoy provider release tag must be vMAJOR.MINOR.PATCH")
        else:
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
    if bundled:
        bundle_kind = "application" if product == "apps" else "driver"
        expected_asset = f"{bundle_kind}-{stable_id}-{version}-{record['architecture']}.rte.zip"
        if len(asset) >= 160:
            raise ValueError("bundle asset exceeds the device catalog name bound")
    if asset != expected_asset:
        raise ValueError(f"asset must be {expected_asset!r}")
    release_repository = record.get("source_repo", REPOSITORY)
    expected_url = f"https://github.com/{release_repository}/releases/download/{tag}/{asset}"
    if record["url"] != expected_url:
        raise ValueError("url must point to the record's immutable GitHub release asset")
    size = record["size"]
    if type(size) is not int or size <= 0:
        raise ValueError("size must be a positive integer")
    if bundled and not 22 <= size <= BUNDLE_MAX_ARCHIVE_BYTES:
        raise ValueError("bundle asset exceeds the ZIP archive size bound")
    digest = record["sha256"]
    if not isinstance(digest, str) or not SHA256_RE.fullmatch(digest):
        raise ValueError("sha256 must be 64 lowercase hexadecimal characters")

    clean = {key: value for key, value in record.items()}
    clean["kind"] = kind
    return clean


def validate_legacy_driver_manifest(manifest: dict[str, Any]) -> None:
    """Retain historical split-file records without treating them as bundles."""
    if {"schema", "kind", "architecture", "artifact", "min_runtime_api", "entries"} & manifest.keys():
        raise ValueError("legacy driver record mixes ordinary bundle metadata")
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


def validate_index_budget(index: dict[str, Any]) -> dict[str, Any]:
    for kind, maximum in MAX_ENTRIES.items():
        entries = index.get(kind)
        if not isinstance(entries, list) or len(entries) > maximum:
            raise ValueError(f"{kind} index exceeds its {maximum}-entry limit")
    return index


def serialize_index(index: dict[str, Any]) -> str:
    """Keep the published index compact while allowing its catalog to grow."""
    return json.dumps(index, separators=(",", ":"), sort_keys=True) + "\n"


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
            stream.write(serialize_index(updated))
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
