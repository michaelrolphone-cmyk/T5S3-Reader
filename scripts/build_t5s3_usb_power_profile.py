#!/usr/bin/env python3
"""Conventional builder for the independent power packages."""
import json
from pathlib import Path

from build_board_power_t5s3_v2 import build

if __name__ == "__main__":
    manifest = Path(__file__).resolve().parents[1] / "Drivers" / "t5s3_usb_power_profile" / "manifest.json"
    build(identities=[json.loads(manifest.read_text(encoding="utf-8"))["id"]])
