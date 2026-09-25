#!/usr/bin/env python3
"""Publish every firmware, app, and driver version newer than the release index."""
from __future__ import annotations

import configparser
import hashlib
import re
import json
import os
import subprocess
import sys
import tempfile
import time
from pathlib import Path
from typing import Any

try:
    from .build_release_record import build_record
    from .update_release_index import update_index, version_tuple
except ImportError:
    from build_release_record import build_record
    from update_release_index import update_index, version_tuple

ROOT = Path(__file__).resolve().parents[1]
INDEX_BRANCH = "release-index"
EMPTY_INDEX = {"schema": 1, "firmware": None, "apps": [], "drivers": []}
VERSION_RE = re.compile(r"(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\Z")


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

    config = configparser.ConfigParser(interpolation=None, strict=False, inline_comment_prefixes=(";", "#"))
    config.read(root / "platformio.ini")
    firmware_version = config.get("riscrte", "version", fallback=None)
    if not isinstance(firmware_version, str) or not VERSION_RE.fullmatch(firmware_version):
        raise ValueError("[riscrte] version in platformio.ini must be numeric MAJOR.MINOR.PATCH")
    firmware_current = version_tuple(firmware_version)
    firmware_entry = index.get("firmware")
    if firmware_entry is not None and not isinstance(firmware_entry, dict):
        raise ValueError("release index firmware entry must be an object or null")
    if firmware_entry is None or firmware_current > version_tuple(firmware_entry.get("version")):
        candidates.append({"product": "firmware", "id": "", "version": firmware_version})

    app_sources = {
        str(path.relative_to(root / "Apps").with_suffix("")).replace("/", "__")
        for path in (root / "Apps").rglob("*.c")
    }
    seen_apps: set[str] = set()
    for path in (root / "Apps").rglob("*.json"):
        if path.name == "app_store.json":
            continue
        try:
            manifest = read_json(path)
        except (OSError, json.JSONDecodeError):
            continue
        if not isinstance(manifest, dict):
            continue
        filename = manifest.get("file_name")
        if not isinstance(filename, str) or not filename.endswith(".elf"):
            continue
        identity = filename[:-4]
        if identity not in app_sources:
            continue
        if identity in seen_apps:
            raise ValueError(f"duplicate app manifest ID {identity}")
        seen_apps.add(identity)
        version = manifest.get("version")
        current = version_tuple(version)
        latest = apps_released.get(identity)
        if latest is None or current > latest:
            candidates.append({"product": "apps", "id": identity, "version": version})

    canonical_driver_ids = {
        "platform-clock-v1", "i2c-esp32s3-v2", "board-power-t5s3-v2",
        "usb-controller-esp32s3", "usb-host-v2", "usb-cdc-acm-v2",
        "usb-cp210x-v2", "usb-ch34x-v2", "usb-ftdi", "usb-stlink",
        "usb-msp", "program-msp", "usb-hid", "usb-hid-keyboard",
        "usb-hid-gamepad", "usb-xinput-gamepad", "usb-ui-navigation",
        "t5s3-usb-power-profile",
    }
    seen_drivers: set[str] = set()
    for path in (root / "Drivers").rglob("manifest.json"):
        try:
            manifest = read_json(path)
        except (OSError, json.JSONDecodeError):
            continue
        if not isinstance(manifest, dict):
            continue
        identity = manifest.get("id")
        if identity not in canonical_driver_ids:
            continue
        if identity in seen_drivers:
            raise ValueError(f"duplicate canonical driver manifest ID {identity}")
        seen_drivers.add(identity)
        version = manifest.get("version")
        current = version_tuple(version)
        latest = drivers_released.get(identity)
        if latest is None or current > latest:
            candidates.append({"product": "drivers", "id": identity, "version": version})

    order = {"firmware": 0, "apps": 1, "drivers": 2}
    return sorted(candidates, key=lambda item: (order[item["product"]], item["id"]))

def release_assets(root: Path, product: str, identity: str) -> list[Path]:
    if product == "firmware":
        config = configparser.ConfigParser(interpolation=None)
        config.read(root / "platformio.ini")
        firmware_version = config.get("riscrte", "version")
        assets = [
            root / "dist/firmware-t5s3-pro.bin",
            root / f"dist/riscrte_lilygo_t5s3_{firmware_version}-app.bin",
            root / f"dist/riscrte_lilygo_t5s3_{firmware_version}.elf",
            root / f"dist/riscrte_lilygo_t5s3_{firmware_version}.bin",
            root / f"firmware/riscrte_lilygo_t5s3_{firmware_version}.bin",
            root / "dist/firmware-lilygo-epd47-s3.bin",
            root / f"dist/riscrte_lilygo_epd47_s3_{firmware_version}-app.bin",
            root / f"dist/riscrte_lilygo_epd47_s3_{firmware_version}.elf",
            root / f"dist/riscrte_lilygo_epd47_s3_{firmware_version}.bin",
            root / f"firmware/riscrte_lilygo_epd47_s3_{firmware_version}.bin",
        ]
    elif product == "apps":
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
    create = ["gh", "release", "create", tag, *[str(path) for path in assets],
              "--title", tag,
              "--notes", f"Independent {candidate['product']} release {candidate['version']} "
                         f"for {candidate['id'] or 'RiscRTE firmware'}.",
              "--target", source_sha]

    def retryable(output: str) -> bool:
        lowered = output.lower()
        return (any(code in lowered for code in ("http 500", "http 502", "http 503",
                                                  "http 504", "http 429")) or
                "rate limit" in lowered or "temporarily unavailable" in lowered or
                "connection reset" in lowered or "timed out" in lowered or
                "already exists" in lowered)

    def wait_before_retry(attempt: int, output: str) -> None:
        delay = min(2 ** (attempt - 1), 20)
        print(f"GitHub release API transient error for {tag}; retry {attempt}/7 "
              f"in {delay}s: {output.strip()[-500:]}", flush=True)
        time.sleep(delay)

    last_error = ""
    for attempt in range(1, 8):
        view = subprocess.run(["gh", "release", "view", tag, "--json", "assets",
                               "--jq", "[.assets[].name]"], cwd=root, text=True, capture_output=True)
        if view.returncode != 0:
            diagnostic = (view.stdout or "") + (view.stderr or "")
            not_found = any(term in diagnostic.lower()
                            for term in ("release not found", "not found", "http 404", "404"))
            if not not_found and retryable(diagnostic):
                last_error = diagnostic
                if attempt < 7:
                    wait_before_retry(attempt, diagnostic)
                    continue
                break
            if not not_found:
                raise RuntimeError(f"could not inspect release {tag}: {diagnostic.strip()}")

            created = subprocess.run(create, cwd=root, text=True, capture_output=True)
            if created.returncode == 0:
                print(f"Created release {tag} with {len(assets)} assets.", flush=True)
                return
            diagnostic = (created.stdout or "") + (created.stderr or "")
            if retryable(diagnostic):
                last_error = diagnostic
                if attempt < 7:
                    wait_before_retry(attempt, diagnostic)
                    continue
                break
            raise subprocess.CalledProcessError(created.returncode, create,
                                                output=created.stdout, stderr=created.stderr)

        existing = json.loads(view.stdout)
        unexpected = sorted(set(existing) - set(expected_names))
        if unexpected:
            raise ValueError(f"existing release {tag} contains unexpected assets: {unexpected}")

        # A prior HTTP error can leave the release record or only some assets
        # behind. Upload every missing asset, and verify the indexed payload
        # whenever it is already present.
        if record["asset"] in existing:
            with tempfile.TemporaryDirectory(prefix="rte-release-verify-") as directory:
                download = subprocess.run(
                    ["gh", "release", "download", tag, "--pattern", record["asset"],
                     "--dir", directory],
                    cwd=root, text=True, capture_output=True)
                if download.returncode != 0:
                    diagnostic = (download.stdout or "") + (download.stderr or "")
                    if retryable(diagnostic) and attempt < 7:
                        last_error = diagnostic
                        wait_before_retry(attempt, diagnostic)
                        continue
                    raise RuntimeError(f"could not verify release asset {record['asset']}: "
                                       f"{diagnostic.strip()}")
                published_asset = Path(directory) / record["asset"]
                digest = hashlib.sha256(published_asset.read_bytes()).hexdigest()
                if digest != record["sha256"]:
                    raise ValueError(f"existing release {tag} has different bytes for {record['asset']}")

        missing = [path for path in assets if path.name not in existing]
        if not missing:
            return
        upload = ["gh", "release", "upload", tag, *[str(path) for path in missing]]
        uploaded = subprocess.run(upload, cwd=root, text=True, capture_output=True)
        if uploaded.returncode == 0:
            print(f"Uploaded {len(missing)} missing assets to {tag}.", flush=True)
            continue
        diagnostic = (uploaded.stdout or "") + (uploaded.stderr or "")
        if retryable(diagnostic):
            last_error = diagnostic
            if attempt < 7:
                wait_before_retry(attempt, diagnostic)
                continue
            break
        raise subprocess.CalledProcessError(uploaded.returncode, upload,
                                            output=uploaded.stdout, stderr=uploaded.stderr)

    raise RuntimeError(f"GitHub release API did not recover for {tag} after 7 attempts: "
                       f"{last_error.strip()}")

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


def publish_all(root: Path = ROOT, planned: list[dict[str, str]] | None = None) -> int:
    source_sha = run(["git", "rev-parse", "HEAD"], cwd=root, capture=True).stdout.strip()
    index, index_exists = current_remote_index(root)
    candidates = discover_candidates(root, index)
    if planned is not None and candidates != planned:
        raise ValueError("release plan no longer matches source manifests or the current index; rerun planning")
    if not candidates:
        print("No firmware, app, or driver versions are newer than the release index.")
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


def main() -> None:
    import argparse

    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--plan-only", action="store_true",
                        help="write newer source-manifest versions to a JSON plan")
    parser.add_argument("--plan-file", type=Path, required=True)
    args = parser.parse_args()
    if args.plan_only:
        index, _exists = current_remote_index(ROOT)
        candidates = discover_candidates(ROOT, index)
        args.plan_file.write_text(json.dumps(candidates, indent=2) + "\n", encoding="utf-8")
        if candidates:
            print("Planned releases: " + ", ".join(
                f"{item['product']} {item['id'] or 'firmware'} v{item['version']}"
                for item in candidates))
        else:
            print("No firmware, app, or driver versions are newer than the release index.")
        return
    planned = json.loads(args.plan_file.read_text(encoding="utf-8"))
    if not isinstance(planned, list):
        raise ValueError("release plan must be a JSON array")
    raise SystemExit(publish_all(ROOT, planned))


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError, KeyError, TypeError, subprocess.CalledProcessError) as exc:
        print(f"RiscRTE release failed: {exc}", file=sys.stderr)
        raise SystemExit(1)
