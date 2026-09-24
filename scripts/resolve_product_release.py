#!/usr/bin/env python3
"""Validate a firmware, app, or driver release request for GitHub Actions."""
from __future__ import annotations

import configparser
import json
import os
import re
import subprocess
from pathlib import Path
from typing import Any

VERSION_RE = re.compile("(0|[1-9][0-9]*)\\\\.(0|[1-9][0-9]*)\\\\.(0|[1-9][0-9]*)\\\\Z")
ID_RE = re.compile("[a-z0-9][a-z0-9._-]{0,63}\\\\Z")


def package_manifest(root: Path, product: str, identity: str) -> tuple[Path, dict[str, Any]]:
    directory = root / ("Apps" if product == "apps" else "Drivers")
    matches: list[tuple[Path, dict[str, Any]]] = []
    for path in directory.rglob("*.json"):
        if path.name in {"app_store.json"} or path.name.endswith("catalog.json"):
            continue
        try:
            manifest = json.loads(path.read_text(encoding="utf-8"))
        except (OSError, json.JSONDecodeError):
            continue
        if not isinstance(manifest, dict):
            continue
        manifest_id = None
        if product == "apps":
            file_name = manifest.get("file_name")
            if isinstance(file_name, str) and file_name.endswith(".elf"):
                manifest_id = file_name[:-4]
        else:
            manifest_id = manifest.get("id")
        if manifest_id == identity:
            matches.append((path, manifest))
    if len(matches) != 1:
        raise ValueError(f"expected exactly one {product[:-1]} manifest for {identity!r}, found {len(matches)}")
    return matches[0]


def resolve(event_name: str, event: dict[str, Any], root: Path) -> dict[str, str]:
    if event_name == "workflow_dispatch":
        inputs = event.get("inputs", {})
        product = inputs.get("product", "")
        identity = inputs.get("id", "")
        version = inputs.get("version", "")
        enabled = True
        commit_firmware = inputs.get("commit_firmware", "false")
        if isinstance(commit_firmware, str):
            if commit_firmware not in ("true", "false"):
                raise ValueError("commit_firmware must be true or false")
            commit_firmware = commit_firmware == "true"
    elif event_name == "push":
        request = json.loads((root / ".github/release-request.json").read_text(encoding="utf-8"))
        expected = {"enabled", "product", "id", "version", "commit_firmware"}
        if set(request) != expected:
            raise ValueError("Request must contain enabled, product, id, version, and commit_firmware only")
        enabled = request["enabled"]
        product, identity, version = request["product"], request["id"], request["version"]
        commit_firmware = request["commit_firmware"]
    else:
        raise ValueError("Unsupported event")
    if type(enabled) is not bool or type(commit_firmware) is not bool:
        raise ValueError("enabled and commit_firmware must be booleans")
    if not enabled:
        return {"publish": "false", "product": "", "id": "", "tag": "", "ver": "", "commit_firmware": "false"}
    if product not in ("firmware", "apps", "drivers"):
        raise ValueError("product must be firmware, apps, or drivers")
    if not isinstance(identity, str) or (product != "firmware" and not ID_RE.fullmatch(identity)):
        raise ValueError("package release requires a safe stable id")
    if product == "firmware" and identity not in ("", "firmware"):
        raise ValueError("firmware id must be empty or 'firmware'")
    if not isinstance(version, str) or not VERSION_RE.fullmatch(version):
        raise ValueError("version must be numeric MAJOR.MINOR.PATCH")
    if product == "firmware":
        config = configparser.ConfigParser(interpolation=None, strict=False, inline_comment_prefixes=(";", "#"))
        config.read(root / "platformio.ini")
        if config.get("riscrte", "version") != version:
            raise ValueError("Firmware version must match [riscrte] version in platformio.ini")
        tag = f"firmware-v{version}"
    else:
        _path, manifest = package_manifest(root, product, identity)
        if manifest.get("version") != version:
            raise ValueError(f"Requested version must match {identity}'s manifest version")
        tag = f"{'app' if product == 'apps' else 'driver'}-{identity}-v{version}"
    return {"publish": "true", "product": product, "id": identity, "tag": tag,
            "ver": version, "commit_firmware": str(commit_firmware).lower()}


def main() -> None:
    outputs = resolve(os.environ["GITHUB_EVENT_NAME"],
                      json.loads(Path(os.environ["GITHUB_EVENT_PATH"]).read_text(encoding="utf-8")),
                      Path("."))
    if outputs["publish"] == "true":
        result = subprocess.run(["git", "show-ref", "--verify", "--quiet", "refs/tags/" + outputs["tag"]])
        if result.returncode == 0:
            raise ValueError("Tag already exists; product release tags are immutable")
        if result.returncode != 1:
            raise RuntimeError("Could not check existing tags")
    with open(os.environ["GITHUB_OUTPUT"], "a", encoding="utf-8") as stream:
        for key, value in outputs.items():
            stream.write(f"{key}={value}\\n")
    print("Product release request validated." if outputs["publish"] == "true"
          else "Release publishing disabled; validation complete.")


if __name__ == "__main__":
    main()
