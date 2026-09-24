#!/usr/bin/env python3
"""Publish every newer canonical app and driver package as its own release."""
from __future__ import annotations

import hashlib
import json
import os
import subprocess
import sys
import tempfile
from pathlib import Path
from typing import Any

from build_release_record import build_record
from update_release_index import update_index, version_tuple

ROOT = Path(__file__).resolve().parents[1]
INDEX_BRANCH = "release-index"
EMPTY_INDEX = {"schema": 1, "firmware": None, "apps": [], "drivers": []}


def read_json(path: Path) -> Any:
    return json.loads(path.read_text(encoding="utf-8"))


def indexed_versions(index: dict[str, Any], kind: str) -> dict[str, tuple[int, int, int]]:
    entries = index.get(kind)
    if not isinstance(entries, list):
        raise ValueError(f"release index {kind} field must be an array")
    result = {}
    for entry in entries:
        if not isinstance(entry, dict) or not isinstance(entry.get("id"), str):
            raise ValueError(f"release index contains a malformed {kind} entry")
        identity = entry["id"]
        if identity in result:
            raise ValueError(f"release index contains duplicate {kind} ID {identity}")
        result[identity] = version_tuple(entry.get("version"))
    return result


def discover_candidates(root: Path, index: dict[str, Any]) -> list[dict[str, str]]:
    if index.get("schema") != 1:
        raise ValueError("release index must use schema 1")
    apps_released = indexed_versions(index, "apps")
    drivers_released = indexed_versions(index, "drivers")
    candidates: list[dict[str, str]] = []

    app_catalog = read_json(root / "dist/apps/app-catalog.json")
    apps = app_catalog.get("apps") if isinstance(app_catalog, dict) else None
    if app_catalog.get("schema") != 1 or not isinstance(apps, list):
        raise ValueError("built app catalog is missing or malformed")
    seen_apps = set()
    for manifest in apps:
        if not isinstance(manifest, dict):
            raise ValueError("built app catalog contains a malformed manifest")
        filename = manifest.get("file_name")
        if not isinstance(filename, str) or not filename.endswith(".elf"):
            raise ValueError("built app catalog contains an invalid ELF name")
        identity = filename[:-4]
        if identity in seen_apps:
            raise ValueError(f"built app catalog contains duplicate ID {identity}")
        seen_apps.add(identity)
        version = manifest.get("version")
        current = version_tuple(version)
        latest = apps_released.get(identity)
        if latest is None or current > latest:
            candidates.append({"product": "apps", "id": identity, "version": version})

    driver_catalog = read_json(root / "dist/packages/usb-provider-catalog.json")
    drivers = driver_catalog.get("packages") if isinstance(driver_catalog, dict) else None
    if driver_catalog.get("schema") != 1 or not isinstance(drivers, list):
        raise ValueError("built canonical driver catalog is missing or malformed")
    seen_drivers = set()
    for package in drivers:
        if not isinstance(package, dict) or not isinstance(package.get("id"), str):
            raise ValueError("built driver catalog contains a malformed package")
        identity, version = package["id"], package.get("version")
        if identity in seen_drivers:
            raise ValueError(f"built driver catalog contains duplicate ID {identity}")
        seen_drivers.add(identity)
        current = version_tuple(version)
        latest = drivers_released.get(identity)
        if latest is None or current > latest:
            candidates.append({"product": "drivers", "id": identity, "version": version})

    return sorted(candidates, key=lambda item: (item["product"], item["id"]))


def release_assets(root: Path, product: str, identity: str) -> list[Path]:
    if product == "apps":
        assets = [root / "dist/apps" / f"{identity}.elf",
                  root / "dist/apps" / f"{identity}.json"]
    else:
        assets = sorted((root / "dist/release-packages").glob(f"{identity}--*"))
    if not assets or any(not path.is_file() or path.stat().st_size == 0 for path in assets):
        raise ValueError(f"missing or empty assets for {product} {identity}")
    return assets


def run(command: list[str], *, cwd: Path = ROOT, capture: bool = False) -> subprocess.CompletedProcess:
    return subprocess.run(command, cwd=cwd, check=True, text=True,
                          capture_output=capture)


def current_remote_index(root: Path) -> tuple[dict[str, Any], bool]:
    refs = run(["git", "ls-remote", "--heads", "origin", f"refs/heads/{INDEX_BRANCH}"],
               cwd=root, capture=True).stdout.strip()
    if not refs:
        return dict(EMPTY_INDEX), False
    run(["git", "fetch", "origin", f"refs/heads/{INDEX_BRANCH}"], cwd=root)
    try:
        raw = run(["git", "show", "FETCH_HEAD:release-index.json"], cwd=root, capture=True).stdout
    except subprocess.CalledProcessError as exc:
        raise ValueError("release-index branch exists but release-index.json is missing") from exc
    index = json.loads(raw)
    if not isinstance(index, dict) or index.get("schema") != 1:
        raise ValueError("release-index branch contains an invalid index")
    return index, True


def checkout_index_branch(root: Path, exists: bool) -> None:
    run(["git", "config", "user.name", "github-actions[bot]"], cwd=root)
    run(["git", "config", "user.email", "41898282+github-actions[bot]@users.noreply.github.com"], cwd=root)
    if exists:
        run(["git", "checkout", "--detach", "FETCH_HEAD"], cwd=root)
        if not (root / "release-index.json").is_file():
            raise ValueError("release-index branch exists but release-index.json is missing")
    else:
        run(["git", "checkout", "--orphan", INDEX_BRANCH], cwd=root)
        run(["git", "rm", "-rf", "--ignore-unmatch", "."], cwd=root)
        (root / "release-index.json").write_text(
            json.dumps(EMPTY_INDEX, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def publish_or_verify(root: Path, source_sha: str, candidate: dict[str, str],
                      record: dict[str, Any], assets: list[Path]) -> None:
    tag = record["tag"]
    expected_names = [path.name for path in assets]
    view = subprocess.run(["gh", "release", "view", tag, "--json", "assets",
                           "--jq", "[.assets[].name]"], cwd=root, text=True, capture_output=True)
    if view.returncode != 0:
        notes = f"Independent {candidate['product']} release {candidate['version']} for {candidate['id']}."
        run(["gh", "release", "create", tag, *[str(path) for path in assets],
             "--title", tag, "--notes", notes, "--target", source_sha], cwd=root)
        return

    existing = json.loads(view.stdout)
    unexpected = sorted(set(existing) - set(expected_names))
    if unexpected:
        raise ValueError(f"existing release {tag} contains unexpected assets: {unexpected}")
    if record["asset"] not in existing:
        raise ValueError(f"existing release {tag} is missing indexed asset {record['asset']}")
    with tempfile.TemporaryDirectory(prefix="rte-release-verify-") as directory:
        run(["gh", "release", "download", tag, "--pattern", record["asset"], "--dir", directory],
            cwd=root)
        published_asset = Path(directory) / record["asset"]
        digest = hashlib.sha256(published_asset.read_bytes()).hexdigest()
        if digest != record["sha256"]:
            raise ValueError(f"existing release {tag} has different bytes for {record['asset']}")
    missing = [path for path in assets if path.name not in existing]
    if missing:
        run(["gh", "release", "upload", tag, *[str(path) for path in missing]], cwd=root)


def write_index(root: Path, index: dict[str, Any], product: str) -> None:
    path = root / "release-index.json"
    with tempfile.NamedTemporaryFile("w", encoding="utf-8", dir=root,
                                     prefix=".release-index.", delete=False) as stream:
        json.dump(index, stream, indent=2, sort_keys=True)
        stream.write("\n")
        stream.flush()
        os.fsync(stream.fileno())
        temporary = Path(stream.name)
    os.replace(temporary, path)
    run(["git", "add", "release-index.json"], cwd=root)
    changed = subprocess.run(["git", "diff", "--cached", "--quiet"], cwd=root)
    if changed.returncode == 1:
        run(["git", "commit", "-m", f"Publish {product} release index entry"], cwd=root)
        run(["git", "push", "origin", f"HEAD:refs/heads/{INDEX_BRANCH}"], cwd=root)
    elif changed.returncode != 0:
        raise RuntimeError("could not inspect release-index changes")


def publish_all(root: Path = ROOT) -> int:
    source_sha = run(["git", "rev-parse", "HEAD"], cwd=root, capture=True).stdout.strip()
    index, index_exists = current_remote_index(root)
    candidates = discover_candidates(root, index)
    if not candidates:
        print("No newer app or canonical driver versions to publish.")
        return 0

    # Check every proposed version and the final index budget before publishing
    # the first package so a policy violation cannot leave a partial batch.
    simulated = index
    records = []
    assets_by_tag = {}
    for candidate in candidates:
        record = build_record(candidate["product"], candidate["id"], candidate["version"], root)
        assets = release_assets(root, candidate["product"], candidate["id"])
        simulated = update_index(simulated, candidate["product"], record)
        records.append((candidate, record))
        assets_by_tag[record["tag"]] = assets

    checkout_index_branch(root, index_exists)
    live_index = read_json(root / "release-index.json")
    if live_index != index:
        raise ValueError("release index changed during bulk release planning; rerun the workflow")

    for candidate, record in records:
        assets = assets_by_tag[record["tag"]]
        publish_or_verify(root, source_sha, candidate, record, assets)
        live_index = update_index(live_index, candidate["product"], record)
        write_index(root, live_index, candidate["product"])
        print(f"Published {candidate['product']} {candidate['id']} v{candidate['version']}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(publish_all())
    except (OSError, ValueError, KeyError, TypeError, subprocess.CalledProcessError) as exc:
        print(f"Bulk package release failed: {exc}", file=sys.stderr)
        raise SystemExit(1)
