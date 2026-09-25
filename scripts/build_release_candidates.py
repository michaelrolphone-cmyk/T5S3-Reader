#!/usr/bin/env python3
"""Build only firmware, apps, and canonical drivers selected by a release plan."""
from __future__ import annotations

import argparse
import configparser
import hashlib
import json
import os
import shutil
import subprocess
import sys
from pathlib import Path
from typing import Any

ROOT = Path(__file__).resolve().parents[1]
FIRMWARE_OUTPUTS = ("firmware.bin", "firmware-merged.bin", "firmware.elf")

DRIVER_BUILDERS = {
    "platform-clock-v1": [("scripts/build_platform_clock_v1.py",)],
    "i2c-esp32s3-v2": [
        ("scripts/probe_i2c_esp32s3_v2.py",),
        ("scripts/audit_i2c_esp32s3_elf.py", "--strict"),
    ],
    "t5s3-usb-power-profile": [("scripts/build_board_power_t5s3_v2.py", "--ids", "t5s3-usb-power-profile")],
    "board-power-t5s3-v2": [("scripts/build_board_power_t5s3_v2.py", "--ids", "board-power-t5s3-v2")],
    "usb-controller-esp32s3": [
        ("scripts/probe_usb_controller_esp32s3.py", "--link-experiment"),
        ("scripts/audit_usb_controller_elf.py", "--strict"),
    ],
    "usb-host-v2": [("scripts/build_usb_host_v2.py",)],
    "usb-cdc-acm-v2": [("scripts/build_usb_cdc_v2.py",)],
    "usb-cp210x-v2": [("scripts/build_usb_cp210x_v2.py",)],
    "usb-ch34x-v2": [("scripts/build_usb_ch34x_v2.py",)],
    "usb-ftdi": [("scripts/build_usb_ftdi.py",)],
    "usb-stlink": [("scripts/build_usb_stlink.py",)],
    "usb-msp": [("scripts/build_usb_msp.py",)],
    "program-msp": [("scripts/build_program_msp.py",)],
    "usb-hid": [("scripts/build_usb_hid.py",)],
    "usb-hid-keyboard": [("scripts/build_usb_hid_keyboard.py",)],
    "usb-hid-gamepad": [("scripts/build_usb_hid_gamepad.py",)],
    "usb-xinput-gamepad": [("scripts/build_usb_xinput_gamepad.py",)],
    "usb-ui-navigation": [("scripts/build_usb_ui_navigation.py",)],
}


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
        run([sys.executable, "scripts/build_all_apps.py", "--id", candidate["id"] + ".elf"])


def build_drivers(candidates: list[dict[str, str]]) -> None:
    identities = sorted(candidate["id"] for candidate in candidates)
    for identity in identities:
        commands = DRIVER_BUILDERS.get(identity)
        if commands is None:
            raise ValueError(f"no selective build is configured for canonical driver {identity}")
        for command in commands:
            run([sys.executable, *command])
    run([sys.executable, "scripts/build_installed_usb_stack.py", "--ids", *identities])
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
    args = parser.parse_args()
    plan: Any = json.loads(args.plan.read_text(encoding="utf-8"))
    if not isinstance(plan, list):
        raise ValueError("release plan must be a JSON array")
    build_plan(plan)


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError, KeyError, TypeError, subprocess.CalledProcessError) as exc:
        print(f"Selective release build failed: {exc}", file=sys.stderr)
        raise SystemExit(1)
