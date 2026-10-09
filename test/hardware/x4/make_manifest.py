#!/usr/bin/env python3
"""Freeze exact checked-out X4 app bytes and workflow provenance."""
import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys


def main():
    if len(sys.argv) != 5:
        raise SystemExit("usage: make_manifest.py FOLDER SOURCE_SHA RUN_ID RUN_ATTEMPT")
    folder = Path(sys.argv[1])
    source_sha = sys.argv[2]
    if not re.fullmatch(r"[0-9a-f]{40}", source_sha):
        raise ValueError("Invalid source SHA")
    if subprocess.check_output(["git", "rev-parse", "HEAD"], text=True).strip() != source_sha:
        raise ValueError("Checkout is not exact PR head")
    run_id, attempt = int(sys.argv[3]), int(sys.argv[4])
    if run_id < 1 or attempt < 1:
        raise ValueError("Invalid workflow run identity")
    image = (folder / "firmware.bin").read_bytes()
    if not (0 < len(image) <= 0x640000 and image[0] == 0xE9
            and b"RISCRTE_BOARD_ID:xteink-x4-pro" in image):
        raise ValueError("Invalid X4 app image")
    manifest = {"schema": 1, "board": "xteink-x4-pro", "source_sha": source_sha,
                "run_id": run_id, "run_attempt": attempt,
                "firmware": {"file": "firmware.bin", "offset": 0x10000,
                             "bytes": len(image), "sha256": hashlib.sha256(image).hexdigest()}}
    (folder / "manifest.json").write_text(json.dumps(manifest, sort_keys=True) + "\n")


if __name__ == "__main__":
    main()
