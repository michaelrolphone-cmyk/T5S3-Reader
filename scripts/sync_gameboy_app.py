#!/usr/bin/env python3
"""Sync the T5S3-GameBoy release into RiscRTE's bounded app index."""
from __future__ import annotations

import json
import re
import subprocess
import sys
import urllib.request
from pathlib import Path
from typing import Any

from update_release_index import update_index, version_tuple

ROOT = Path(__file__).resolve().parents[1]
GAMEBOY_REPOSITORY = "michaelrolphone-cmyk/T5S3-GameBoy"
INDEX_BRANCH = "release-index"
EMPTY_INDEX = {"schema": 1, "firmware": None, "apps": [], "drivers": []}
TAG_RE = re.compile(r"v(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\Z")
SHA256_RE = re.compile(r"[0-9a-f]{64}\Z")
MAX_MANIFEST_BYTES = 8192
MAX_APP_BYTES = 8 * 1024 * 1024


def release_asset(release: dict[str, Any], name: str) -> dict[str, Any]:
    assets = release.get("assets")
    if not isinstance(assets, list):
        raise ValueError("latest GameBoy release has no asset list")
    matches = [asset for asset in assets if isinstance(asset, dict) and asset.get("name") == name]
    if len(matches) != 1:
        raise ValueError(f"latest GameBoy release must contain exactly one {name}")
    asset = matches[0]
    tag = release.get("tag_name")
    expected_url = f"https://github.com/{GAMEBOY_REPOSITORY}/releases/download/{tag}/{name}"
    if asset.get("browser_download_url") != expected_url:
        raise ValueError(f"GameBoy release asset URL is invalid for {name}")
    return asset


def record_from_release(release: Any, manifest: Any) -> dict[str, Any]:
    if not isinstance(release, dict) or release.get("draft") or release.get("prerelease"):
        raise ValueError("latest GameBoy release must be a published stable release")
    tag = release.get("tag_name")
    if not isinstance(tag, str) or not TAG_RE.fullmatch(tag):
        raise ValueError("GameBoy release tag must be vMAJOR.MINOR.PATCH")
    version = tag[1:]
    version_tuple(version)
    if not isinstance(manifest, dict):
        raise ValueError("GameBoy app manifest must be a JSON object")
    if (manifest.get("display_name") != "GameBoy" or
            manifest.get("file_name") != "gameboy.elf" or
            not isinstance(manifest.get("version"), str) or
            not isinstance(manifest.get("min_firmware_version"), str)):
        raise ValueError("GameBoy release manifest is missing its app identity or version fields")
    version_tuple(manifest["version"])
    version_tuple(manifest["min_firmware_version"])
    size = manifest.get("size_bytes")
    digest = manifest.get("sha256")
    if type(size) is not int or size < 52 or size > MAX_APP_BYTES:
        raise ValueError("GameBoy manifest size is outside the supported ELF limit")
    if not isinstance(digest, str) or not SHA256_RE.fullmatch(digest):
        raise ValueError("GameBoy manifest must include a lowercase SHA-256 digest")
    elf = release_asset(release, "gameboy.elf")
    sidecar = release_asset(release, "gameboy.json")
    if type(elf.get("size")) is not int or elf["size"] != size:
        raise ValueError("GameBoy release ELF size does not match its manifest")
    api_digest = elf.get("digest")
    if api_digest is not None and api_digest != f"sha256:{digest}":
        raise ValueError("GameBoy release ELF digest does not match its manifest")
    if type(sidecar.get("size")) is not int or sidecar["size"] > MAX_MANIFEST_BYTES:
        raise ValueError("GameBoy release manifest asset exceeds its size limit")
    return {
        "id": "gameboy",
        "version": manifest["version"],
        "tag": tag,
        "asset": "gameboy.elf",
        "url": elf["browser_download_url"],
        "size": size,
        "sha256": digest,
        "manifest": manifest,
        "source_repo": GAMEBOY_REPOSITORY,
    }


def upsert_gameboy_app(index: Any, record: dict[str, Any]) -> tuple[dict[str, Any], bool]:
    if not isinstance(index, dict) or index.get("schema") != 1:
        raise ValueError("release index must use schema 1")
    apps = index.get("apps")
    if not isinstance(apps, list):
        raise ValueError("release index apps field must be an array")
    previous = next((item for item in apps if isinstance(item, dict) and item.get("id") == "gameboy"), None)
    if previous is not None:
        old_version = version_tuple(previous.get("version"))
        new_version = version_tuple(record["version"])
        if new_version < old_version:
            raise ValueError("latest GameBoy app release would move its indexed version backwards")
        if new_version == old_version:
            same_payload = (previous.get("sha256") == record["sha256"] and
                            previous.get("size") == record["size"] and
                            previous.get("manifest") == record["manifest"])
            if same_payload:
                return index, False
            raise ValueError("GameBoy app content changed without an app manifest version bump")
    updated = update_index(index, "apps", record)
    return updated, updated != index


def _run(command: list[str], *, capture: bool = False) -> subprocess.CompletedProcess[str]:
    return subprocess.run(command, cwd=ROOT, check=True, text=True, capture_output=capture)


def fetch_latest_release() -> tuple[dict[str, Any], dict[str, Any]]:
    response = _run(["gh", "api", f"repos/{GAMEBOY_REPOSITORY}/releases/latest"], capture=True)
    release = json.loads(response.stdout)
    manifest_asset = release_asset(release, "gameboy.json")
    request = urllib.request.Request(
        manifest_asset["browser_download_url"],
        headers={"Accept": "application/octet-stream", "User-Agent": "RiscRTE-GameBoy-Index-Sync"},
    )
    with urllib.request.urlopen(request, timeout=30) as stream:
        raw = stream.read(MAX_MANIFEST_BYTES + 1)
    if not raw or len(raw) > MAX_MANIFEST_BYTES:
        raise ValueError("GameBoy release manifest is empty or exceeds its size limit")
    manifest = json.loads(raw.decode("utf-8"))
    return release, manifest


def sync_index(record: dict[str, Any]) -> bool:
    refs = _run(["git", "ls-remote", "--heads", "origin", f"refs/heads/{INDEX_BRANCH}"],
                capture=True).stdout.strip()
    if not refs:
        raise ValueError(f"remote {INDEX_BRANCH} branch does not exist")
    _run(["git", "fetch", "origin", f"refs/heads/{INDEX_BRANCH}"])
    raw_index = _run(["git", "show", "FETCH_HEAD:release-index.json"], capture=True).stdout
    index = json.loads(raw_index)
    updated, changed = upsert_gameboy_app(index, record)
    if not changed:
        print(f"GameBoy app v{record['version']} is already indexed.")
        return False

    _run(["git", "config", "user.name", "github-actions[bot]"])
    _run(["git", "config", "user.email",
          "41898282+github-actions[bot]@users.noreply.github.com"])
    _run(["git", "checkout", "--detach", "FETCH_HEAD"])
    path = ROOT / "release-index.json"
    path.write_text(json.dumps(updated, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    _run(["git", "add", "release-index.json"])
    changed_files = subprocess.run(["git", "diff", "--cached", "--quiet"], cwd=ROOT)
    if changed_files.returncode == 0:
        return False
    if changed_files.returncode != 1:
        raise RuntimeError("could not inspect release-index changes")
    _run(["git", "commit", "-m", f"Index GameBoy app v{record['version']}"])
    _run(["git", "push", "origin", f"HEAD:refs/heads/{INDEX_BRANCH}"])
    print(f"Indexed GameBoy app v{record['version']} from {record['source_repo']}.")
    return True


def main() -> int:
    release, manifest = fetch_latest_release()
    record = record_from_release(release, manifest)
    sync_index(record)
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ValueError, KeyError, TypeError, subprocess.CalledProcessError,
            urllib.error.URLError, json.JSONDecodeError) as exc:
        print(f"GameBoy app index sync failed: {exc}", file=sys.stderr)
        raise SystemExit(1)
