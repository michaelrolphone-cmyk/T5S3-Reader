"""Pinned X4 Pro artifact and diagnostic contract. No physical I/O or flash code."""
import hashlib
import io
import json
from pathlib import Path
import re
import struct
import time
import zipfile

TARGET = "xteink-x4-pro"
MAC = "84:c7:bb:79:e2:ac"
CHIP = "ESP32-S3"
WORKFLOW = "x4-hardware-build.yml"
PREFIX = "x4-candidate-"
IMAGE = "riscrte-xteink-x4-pro.bin"
MAX_IMAGE = 0x640000
MAX_ZIP = MAX_IMAGE + 65536
SHA = re.compile(r"[0-9a-f]{40}\Z")
MARKER = b"RISCRTE_BOARD_ID:xteink-x4-pro"
TESTS = ("boot", "panel", "storage", "input")


def require(ok, reason):
    if not ok:
        raise ValueError(reason)


def image_digest(image):
    require(24 <= len(image) <= MAX_IMAGE, "X4 app image size invalid")
    require(image[0] == 0xe9 and struct.unpack_from("<H", image, 12)[0] == 9,
            "X4 app image must target ESP32-S3")
    require(MARKER + b"\x00" in image, "X4 firmware model marker absent")
    return hashlib.sha256(image).hexdigest()


def manifest(image, sha, run_id, attempt):
    require(SHA.fullmatch(sha) is not None and type(run_id) is int and run_id > 0
            and type(attempt) is int and attempt > 0, "Invalid exact build provenance")
    return {"schema": 1, "target": TARGET, "chip": CHIP, "source_sha": sha,
            "run_id": run_id, "run_attempt": attempt,
            "firmware": {"file": IMAGE, "bytes": len(image), "sha256": image_digest(image),
                         "offset": 0x10000, "max_bytes": MAX_IMAGE}}


def unpack(gh, run, artifact, sha, folder):
    raw = gh.call(f"/actions/artifacts/{artifact['id']}/zip", limit=MAX_ZIP)
    require(len(raw) <= MAX_ZIP, "X4 archive exceeds bound")
    with zipfile.ZipFile(io.BytesIO(raw)) as archive:
        entries = archive.infolist()
        require(len(entries) == 2 and {e.filename for e in entries} == {IMAGE, "manifest.json"},
                "X4 artifact entries invalid")
        for entry in entries:
            require(not entry.flag_bits & 1 and entry.file_size <=
                    (MAX_IMAGE if entry.filename == IMAGE else 4096), "X4 entry exceeds bound")
            require((entry.external_attr >> 16) & 0o170000 != 0o120000, "X4 symlink refused")
        image = archive.read(IMAGE)
        info = json.loads(archive.read("manifest.json"))
    expected = manifest(image, sha, run["id"], run["run_attempt"])
    require(info == expected, "X4 artifact provenance, target or digest mismatch")
    folder.mkdir(mode=0o700)
    (folder / IMAGE).write_bytes(image)
    (folder / "manifest.json").write_text(json.dumps(info, sort_keys=True) + "\n")
    return info["firmware"]["sha256"]


def identity(usb, chip, mac, model):
    # A path is only a locator. Require native USB identity AND independently
    # read chip/MAC plus the pinned physical model binding before future I/O.
    require(usb.get("vid") == 0x303a and usb.get("pid") == 0x1001
            and str(usb.get("serial_number", "")).lower() == MAC
            and chip == CHIP and str(mac).lower() == MAC and model == TARGET,
            "X4 model/chip/MAC mismatch")


def observe(stream, sha, digest, *, clock=time.monotonic, seconds=90):
    """Consume an already-open bounded stream; never opens a port or stores lines.

    Caller owns shared lock, identity, exact candidate readback and <=1s read
    timeout. This parser is host-tested only; current live adapter is disabled.
    Missing evidence stays missing. Panel means serial completion, not optical proof.
    """
    require(SHA.fullmatch(sha) is not None and re.fullmatch(r"[0-9a-f]{64}", digest),
            "Exact firmware identity required")
    require(0 < seconds <= 90, "Observation deadline exceeds bound")
    checks = {key: "missing" for key in TESTS}
    counts = {key: 0 for key in TESTS}
    deadline = clock() + seconds
    pending = bytearray()
    total = 0
    rows = 0
    error = None
    try:
        while clock() < deadline and total < 256 * 1024 and rows < 2000:
            data = stream.read(512)  # adapter must enforce <=1 second timeout
            require(isinstance(data, bytes) and len(data) <= 512, "Invalid serial chunk")
            total += len(data)
            pending.extend(data)
            while b"\n" in pending:
                row, _, pending = pending.partition(b"\n")
                rows += 1
                require(rows <= 2000 and len(row) <= 512, "X4 serial bound exceeded")
                if any(x in row for x in (b"Guru Meditation", b"Backtrace:", b"panic", b"abort()")):
                    raise ValueError("X4 runtime failure")
                # Only match bounded known diagnostics, never retain arbitrary
                # text, private images, storage contents, or provider error strings.
                line = row.decode("ascii", "replace").rstrip("\r")
                message = re.fullmatch(r"\[\d{1,12}\] \[(?:INF|ERR|DBG|WRN)\] \[X4\] (.*)", line)
                if not message:
                    continue
                msg = message[1]
                if "failed" in msg or "rejected" in msg:
                    raise ValueError("X4 diagnostic failure")
                key = None
                if msg == "diagnostic boot RISCRTE_BOARD_ID:xteink-x4-pro flash=16MB app0=0x10000":
                    key = "boot"
                elif re.fullmatch(r"present complete elapsed=\d{1,5}(?: [ -~]{0,200})?", msg):
                    key = "panel"
                elif re.fullmatch(r"storage.volume mounted=1 reason=[A-Za-z0-9_-]{1,64}", msg):
                    key = "storage"
                elif re.fullmatch(r"input.navigation event=(left|right|confirm|back) sequence=\d{1,8}", msg):
                    key = "input"
                if key:
                    counts[key] += 1
                    checks[key] = "pass"
                    require(counts["boot"] <= 1, "X4 reboot detected")
            require(len(pending) <= 512, "X4 serial line bound exceeded")
            # Read blocks cooperatively on real adapters; also yield for empty
            # nonblocking fixtures. Observe the full window to catch later faults.
            time.sleep(0.001)
        if total >= 256 * 1024 or rows >= 2000:
            error = "serial_limit"
    except (OSError, ValueError):
        error = "serial_failure"  # never export arbitrary exception payload
    return {"target": TARGET, "source_sha": sha, "firmware_sha256": digest,
            "checks": checks, "counts": counts, "error": error,
            "result": "pass" if error is None and all(v == "pass" for v in checks.values()) else "failed"}


def live(*_args, **_kwargs):
    raise RuntimeError("X4 live execution disabled: unresolved execution approval; no device access")


if __name__ == "__main__":
    import argparse
    parser = argparse.ArgumentParser(description="Freeze X4 application manifest (cloud only)")
    parser.add_argument("folder", type=Path)
    parser.add_argument("sha")
    parser.add_argument("run_id", type=int)
    parser.add_argument("attempt", type=int)
    args = parser.parse_args()
    path = args.folder / IMAGE
    require(path.stat().st_size <= MAX_IMAGE, "X4 application exceeds bound")
    (args.folder / "manifest.json").write_text(json.dumps(
        manifest(path.read_bytes(), args.sha, args.run_id, args.attempt), sort_keys=True) + "\n")
