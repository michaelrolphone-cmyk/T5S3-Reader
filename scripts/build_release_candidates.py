#!/usr/bin/env python3
"""Build only firmware, apps, and canonical drivers selected by a release plan."""
from __future__ import annotations

import argparse
import configparser
import json
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path
from typing import Any

if __package__:
    from .update_release_index import PRODUCTS
    from .build_installed_usb_stack import source_candidates
else:
    from update_release_index import PRODUCTS
    from build_installed_usb_stack import source_candidates

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


def discover_module_sources(root: Path) -> list[dict]:
    """Use ordinary package discovery; fail before any ambiguous flat output."""
    sources = source_candidates(root, allow_empty=True)
    module_ids = {item['id'] for item in sources}
    for item in sources:
        expected = {'driver': 'Drivers', 'service': 'Services', 'provider': 'Providers'}[item['metadata']['type']]
        if item['source'].parent.parent.name != expected:
            raise ValueError('provider source root/kind mismatch')
    for index, path in enumerate((root / 'Apps').rglob('*.json')):
        if index >= 128:
            raise ValueError('application source inventory exceeds bound')
        if path.is_symlink() or path.stat().st_size > 4096:
            raise ValueError('unsafe or oversized application source manifest')
        manifest = json.loads(path.read_text(encoding='utf-8'))
        if isinstance(manifest, dict) and isinstance(manifest.get('file_name'), str) and manifest['file_name'].removesuffix('.elf') in module_ids:
            raise ValueError('application/module identity collides in flat build output')
    return sources


def discover_module_builders(root: Path, kind: str) -> dict[str, list[tuple[str, ...]]]:
    if kind not in ('driver', 'service', 'provider'):
        raise ValueError('invalid module build kind')
    builders = {}
    for item in discover_module_sources(root):
        if item['metadata']['type'] != kind:
            continue
        if item['resources_only']:
            builders[item['id']] = []  # Common declarative packager, no invented build script.
            continue
        source = item['source'].parent
        if not re.fullmatch(r'[a-z0-9_]+', source.name):
            raise ValueError(f'unsafe provider build source: {source}')
        conventional = f'scripts/build_{source.name}.py'
        commands = (_BOOTSTRAP_BUILD_RECIPES.get(source.name, [(conventional,)])
                    if kind == 'driver' else [(conventional,)])
        for command in commands:
            script = root / command[0]
            if not script.is_file() or script.is_symlink():
                raise ValueError(f"{item['id']}: missing or unsafe provider builder {command[0]}")
        builders[item['id']] = commands
    return builders


def discover_driver_builders(root: Path) -> dict[str, list[tuple[str, ...]]]:
    return discover_module_builders(root, 'driver')


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
    discover_module_sources(ROOT)  # Validate flat app/module output identities first.
    for candidate in candidates:
        run([sys.executable, "scripts/build_all_apps.py", "--id", candidate["id"]])
    if candidates:
        run([sys.executable, "scripts/export_canonical_driver_release.py", "--output",
             "dist/release-app-packages", "--kind", "application", "--ids", *sorted(item["id"] for item in candidates)])


def build_drivers(candidates: list[dict[str, str]], kind: str = 'driver') -> None:
    identities = sorted(candidate["id"] for candidate in candidates)
    builders = discover_module_builders(ROOT, kind)
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
    output = 'dist/release-packages' if kind == 'driver' else f'dist/release-{kind}-packages'
    run([sys.executable, "scripts/export_canonical_driver_release.py", "--output", output,
         "--kind", kind, "--ids", *identities])


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
    if __package__:
        from .verify_release_plan import validate_plan
    else:
        from verify_release_plan import validate_plan
    validate_plan(plan)
    if not plan:
        print("Nothing changed; no product builds are needed.")
        return
    products = {candidate["product"] for candidate in plan}
    if products - {"firmware"}:
        discover_module_sources(ROOT)
    if "firmware" in products:
        build_firmware()
    if "apps" in products:
        build_apps([item for item in plan if item["product"] == "apps"])
    if "drivers" in products:
        build_drivers([item for item in plan if item["product"] == "drivers"])
    for product in ('services', 'providers'):
        if product in products:
            build_drivers([item for item in plan if item['product'] == product], product[:-1])
    if "firmware" in products:
        stage_firmware()
    print("Built only planned changed products: " + ", ".join(
        f"{item['product']} {item['id'] or 'firmware'} v{item['version']}" for item in plan))


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--plan", type=Path, required=True)
    parser.add_argument("--product", choices=PRODUCTS)
    parser.add_argument("--id", help="build only this planned package ID")
    args = parser.parse_args()
    plan: Any = json.loads(args.plan.read_text(encoding="utf-8"))
    if not isinstance(plan, list):
        raise ValueError("release plan must be a JSON array")
    if args.id and not args.product:
        parser.error("--id requires --product")
    if __package__:
        from .verify_release_plan import validate_plan
    else:
        from verify_release_plan import validate_plan
    validate_plan(plan)
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
