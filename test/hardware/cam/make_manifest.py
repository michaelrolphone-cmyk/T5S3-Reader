"""Generate data-only provenance for a CAM candidate; no captured images."""
import hashlib
import json
import re
import sys
from pathlib import Path


def main():
    folder, source_sha, run_id, attempt = sys.argv[1:]
    if not re.fullmatch(r"[0-9a-f]{40}", source_sha):
        raise ValueError("source SHA must be an exact commit")
    image = Path(folder) / "firmware.bin"
    size = image.stat().st_size
    if not 0 < size <= 2 * 1024 * 1024:
        raise ValueError("CAM firmware size out of bounds")
    payload = {
        "schema": 1, "source_sha": source_sha,
        "run_id": int(run_id), "run_attempt": int(attempt),
        "firmware": {"file": "firmware.bin", "bytes": size,
                     "sha256": hashlib.sha256(image.read_bytes()).hexdigest()},
    }
    (Path(folder) / "manifest.json").write_text(json.dumps(payload, sort_keys=True) + "\n")


if __name__ == "__main__":
    main()
