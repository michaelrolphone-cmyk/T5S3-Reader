#!/usr/bin/env python3
"""Build only firmware, apps, and canonical drivers selected by a release plan."""
from __future__ import annotations

import argparse
import configparser
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path
from typing import Any

if __package__:
    from .generate_provider_package_inputs_v1 import canonical_manifest
    from .update_release_index import version_tuple
else:
    from generate_provider_package_inputs_v1 import canonical_manifest
    from update_release_index import version_tuple

ROOT = Path(__file__).resolve().parents[1]
FIRMWARE_OUTPUTS = ("firmware.bin", "firmware-merged.bin", "firmware.elf")

# These two source directories still have physical link/audit probes instead
# of a conventional build_<source>.py entry point. This is a build recipe,
# never the set of allowed package IDs; identities come only from manifests.
_BOOTSTRAP_BUILD_RECIPES = {
    "i2c_esp32s3_v2": [
        ("scripts/probe_i2c_esp32s3_v2.py",),
        ("scripts/audit_i2c_esp32s3_elf.py", "--strict"),
    ],
    "usb_controller_esp32s3": [
        ("scripts/probe_usb_controller_esp32s3.py", "--link-experiment"),
        ("scripts/audit_usb_controller_elf.py", "--strict"),
    ],
}
MAX_DRIVER_PACKAGES = 64


def discover_driver_builders(root: Path) -> dict[str, list[tuple[str, ...]]]:
    """Read bounded source metadata without compiling or requiring ELF output."""
    builders: dict[str, list[tuple[str, ...]]] = {}
    for manifest_path in sorted((root / "Drivers").glob("*/manifest.json")):
        source = manifest_path.parent
        if (source.is_symlink() or manifest_path.is_symlink() or
                not re.fullmatch(r"[a-z0-9_]+", source.name)):
            raise ValueError(f"unsafe provider build source: {source}")
        if manifest_path.stat().st_size > 4096:
            raise ValueError(f"provider manifest is oversized: {manifest_path}")
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
        if not isinstance(manifest, dict):
            raise ValueError(f"invalid provider manifest: {manifest_path}")
        if manifest.get("type") != "driver" or manifest.get("driver_abi") != 2:
            continue  # Historical ABI-1 inputs are not current release packages.
        canonical_manifest(manifest_path)
        identity = manifest["id"]
        version_tuple(manifest.get("version"))
        if identity in builders:
            raise ValueError(f"duplicate canonical driver manifest ID {identity}")
        if len(builders) >= MAX_DRIVER_PACKAGES:
            raise ValueError("provider count exceeds firmware package catalog bound")
        conventional = f"scripts/build_{source.name}.py"
        commands = _BOOTSTRAP_BUILD_RECIPES.get(source.name, [(conventional,)])
        for command in commands:
            script = root / command[0]
            if not script.is_file() or script.is_symlink():
                raise ValueError(f"{identity}: missing or unsafe provider builder {command[0]}")
        builders[identity] = commands
    return builders


def run(command: list[str]) -> None:
    print("+ " + " ".join(command), flush=True)
    subprocess.run(command, cwd=ROOT, check=True)


def firmware_version() -> str:
    config = configparser.ConfigParser(interpolation=None, strict=False, inline_comment_prefixes=(";", "#"))
    config.read(ROOT / "platformio.ini")
    version = config.get("riscrte", "version", fallback=None)
    if not isinstance(version, str) or not version:
        raise ValueError("missing [riscrte] version in platformio.ini")
    return version


def build_firmware() -> None:
    run(["pio", "run", "-e", "gh_release"])
    output = ROOT / ".pio/build/gh_release"
    preserved = Path(os.environ["RUNNER_TEMP"]) / "riscrte-firmware"
    preserved.mkdir(parents=True, exist_ok=True)
    for name in FIRMWARE_OUTPUTS:
        source = output / name
        if not source.is_file() or source.stat().st_size == 0:
            raise ValueError(f"firmware build did not produce {source}")
        shutil.copyfile(source, preserved / name)
    sums = subprocess.run(["sha256sum", *[str(preserved / name) for name in FIRMWARE_OUTPUTS]],
                          cwd=ROOT, check=True, text=True, capture_output=True).stdout
    (preserved / "SHA256SUMS").write_text(sums, encoding="utf-8")
    binary = (preserved / "firmware.bin").read_bytes()
    version = firmware_version().encode("ascii")
    if version not in binary or b"-dev-" in binary:
        raise ValueError("firmware image does not contain the configured clean RiscRTE version")


def build_apps(candidates: list[dict[str, str]]) -> None:
    for candidate in candidates:
        run([sys.executable, "scripts/build_all_apps.py", "--id", candidate["id"]])


def build_drivers(candidates: list[dict[str, str]]) -> None:
    identities = sorted(candidate["id"] for candidate in candidates)
    builders = discover_driver_builders(ROOT)
    if len(identities) != len(set(identities)):
        raise ValueError("release plan contains duplicate driver IDs")
    missing = set(identities) - builders.keys()
    if missing:
        raise ValueError(f"no source builder for requested driver IDs: {sorted(missing)}")
    if not identities:
        return
    for identity in identities:
        for command in builders[identity]:
            run([sys.executable, *command])
    run([sys.executable, "scripts/build_installed_usb_stack.py", "--ids", *identities])
    run([sys.executable, "test/drivers/installed_usb_stack_package_test.py", "--ids", *identities])
    run([sys.executable, "scripts/export_canonical_driver_release.py", "--ids", *identities])


def stage_firmware() -> None:
    temp = Path(os.environ["RUNNER_TEMP"])
    preserved = temp / "riscrte-firmware"
    subprocess.run(["sha256sum", "-c", str(preserved / "SHA256SUMS")],
                   cwd=ROOT, check=True)
    version = firmware_version()
    dist = ROOT / "dist"
    firmware = ROOT / "firmware"
    dist.mkdir(exist_ok=True)
    firmware.mkdir(exist_ok=True)
    shutil.copyfile(preserved / "firmware.bin", dist / "firmware-t5s3-pro.bin")
    shutil.copyfile(preserved / "firmware.bin", dist / f"riscrte_lilygo_t5s3_{version}-app.bin")
    shutil.copyfile(preserved / "firmware-merged.bin", dist / f"riscrte_lilygo_t5s3_{version}.bin")
    shutil.copyfile(preserved / "firmware.elf", dist / f"riscrte_lilygo_t5s3_{version}.elf")
    shutil.copyfile(preserved / "firmware-merged.bin", firmware / f"riscrte_lilygo_t5s3_{version}.bin")


def build_plan(plan: list[dict[str, str]]) -> None:
    if not plan:
        print("Nothing changed; no product builds are needed.")
        return
    products = {candidate["product"] for candidate in plan}
    if "firmware" in products:
        build_firmware()
    if "apps" in products:
        build_apps([item for item in plan if item["product"] == "apps"])
    if "drivers" in products:
        build_drivers([item for item in plan if item["product"] == "drivers"])
    if "firmware" in products:
        stage_firmware()
    print("Built only planned changed products: " + ", ".join(
        f"{item['product']} {item['id'] or 'firmware'} v{item['version']}" for item in plan))


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--plan", type=Path, required=True)
    parser.add_argument("--product", choices=("firmware", "apps", "drivers"))
    parser.add_argument("--id", help="build only this planned package ID")
    args = parser.parse_args()
    plan: Any = json.loads(args.plan.read_text(encoding="utf-8"))
    if not isinstance(plan, list):
        raise ValueError("release plan must be a JSON array")
    if args.id and not args.product:
        parser.error("--id requires --product")
    selected = [item for item in plan
                if (args.product is None or item.get("product") == args.product)
                and (args.id is None or item.get("id") == args.id)]
    if args.product and not selected:
        print(f"No changed {args.product} candidates to build.")
        return
    build_plan(selected)


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError, KeyError, TypeError, subprocess.CalledProcessError) as exc:
        print(f"Selective release build failed: {exc}", file=sys.stderr)
        raise SystemExit(1)
