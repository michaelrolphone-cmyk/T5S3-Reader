#!/usr/bin/env python3
"""Pinned X4 app0 hardware check. Never executes code from a PR artifact on macOS."""
import argparse
import fcntl
import hashlib
import json
import re
import signal
import struct
import subprocess
import sys
import time
import zlib
from pathlib import Path

import serial
from serial.tools import list_ports

MAC = "84:c7:bb:79:e2:ac"
TABLE_SHA = "9af3af2b74e944337ba85f2b0027ee80df160579a1ab746ba0f95853f618cd60"
BACKUP = Path("/Users/micahelbyrns/Documents/Codex/2026-10-01/task-6/lab-records/x4-pr350-artifact/full-afebe/x4-full-flash-backup.bin")
BACKUP_SHA = "82fca32e37ff6577eafd6f102c1e78d10ac8f58d233e7d6b9b1fbdc15147b75e"
LOCK_DIR = Path("/Users/micahelbyrns/Documents/Codex/2026-09-30/task/.device-locks")
APP_OFFSET, APP_END = 0x10000, 0x650000


def require(ok, message):
    if not ok:
        raise RuntimeError(message)


def digest(data):
    return hashlib.sha256(data).hexdigest()


def port_now(location):
    found = [p for p in list_ports.comports()
             if (p.serial_number or "").lower() == MAC and (p.vid, p.pid) == (0x303a, 0x1001)
             and p.location == location]
    require(len(found) == 1, "X4 USB identity absent or ambiguous")
    return found[0].device


def ota_app0(data):
    valid = []
    for slot in (0, 1):
        entry = data[slot * 4096:slot * 4096 + 32]
        seq = struct.unpack_from("<I", entry)[0]
        state, crc = struct.unpack_from("<II", entry, 24)
        if seq != 0xffffffff and state not in (3, 4) and crc == zlib.crc32(struct.pack("<I", seq), 0xffffffff):
            valid.append(seq)
    return bool(valid) and (max(valid) - 1) % 2 == 0


def validate_image(image, expected):
    erase_end = APP_OFFSET + ((len(image) + 4095) // 4096) * 4096
    require(re.fullmatch(r"[0-9a-f]{64}", expected) is not None and digest(image) == expected,
            "X4 candidate digest mismatch")
    require(0 < len(image) <= 0x640000 and image[0] == 0xe9
            and b"RISCRTE_BOARD_ID:xteink-x4-pro" in image and erase_end <= APP_END,
            "X4 candidate app0 image incompatible")
    return erase_end


def validate_boot(lines):
    def count(fragment):
        return sum(fragment in line for line in lines)
    require(count("diagnostic entered") == 1, "X4 diagnostic did not enter exactly once")
    require(count("diagnostic boot RISCRTE_BOARD_ID:xteink-x4-pro") == 1,
            "X4 board identity or boot count mismatch")
    require(count("storage.volume mounted=1") == 1, "X4 storage mount missing")
    require(count("boot splash present=1") == 1, "X4 startup present missing")
    require(count("home geometry width=480 height=800 menu_height=") == 1,
            "X4 portrait Home geometry missing")
    geometry = next(line for line in lines if "home geometry width=480 height=800 menu_height=" in line)
    match = re.search(r"menu_height=(\d+)", geometry)
    require(match is not None and int(match[1]) >= 151, "X4 Home menu does not fit")
    require(count("home activity scheduled=1") == 1 and count("home present=1") == 1,
            "X4 Home present missing or repeated")
    ready = count("heartbeat ready=1")
    require(ready >= 3, "X4 steady heartbeat missing")
    require(not any(x in line for line in lines for x in ("Guru Meditation", "abort()", "Backtrace:", "[ERR] [X4]")),
            "X4 panic or boot error")
    return {"diagnostic_once": True, "storage_mounted": True, "boot_splash_present": True,
            "home_present": True, "home_geometry": "480x800", "menu_height": int(match[1]),
            "ready_heartbeats": ready, "serial_reset_only": True,
            "visual_qualified": False, "buttons_qualified": False, "txt_qualified": False}


def validate_restored_boot(lines):
    def count(fragment):
        return sum(fragment in line for line in lines)
    require(count("diagnostic entered") == 1 and count("home present=1") == 1
            and count("heartbeat ready=1") >= 3,
            "X4 restored prewrite app did not return to steady Home")
    return {"home_present": True, "ready_heartbeats": count("heartbeat ready=1")}


def observe(port, location, seconds=38):
    # Retain only tightly whitelisted numeric diagnostics, never TXT content.
    deadline = time.monotonic() + seconds
    pending = bytearray()
    lines = []
    received = 0
    with serial.Serial(port, 115200, timeout=0.5, exclusive=True) as handle:
        while time.monotonic() < deadline and received < 1_048_576 and len(lines) < 300:
            require(port_now(location) == port, "X4 port changed during boot capture")
            chunk = handle.read(4096)
            received += len(chunk)
            pending.extend(chunk)
            while b"\n" in pending:
                raw, _, remainder = pending.partition(b"\n")
                pending = bytearray(remainder)
                line = raw.decode("utf-8", "replace").rstrip("\r")
                if len(line) <= 256 and ("[X4] diagnostic" in line or "[X4] storage.volume mounted=" in line
                        or "[X4] boot splash present=" in line or "[X4] home geometry " in line
                        or "[X4] home activity scheduled=" in line or "[X4] home present=" in line
                        or "[X4] heartbeat ready=" in line or "Guru Meditation" in line
                        or "abort()" in line or "Backtrace:" in line or "[ERR] [X4]" in line):
                    lines.append(line)
            require(len(pending) <= 4096, "X4 serial line exceeds bound")
            if sum("heartbeat ready=1" in line for line in lines) >= 3:
                break
    return lines, received


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--image", type=Path, required=True)
    parser.add_argument("--sha256", required=True)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--location", required=True)
    args = parser.parse_args()
    require(re.fullmatch(r"[0-9]+-[0-9]+(?:\.[0-9]+)*", args.location) is not None,
            "Invalid X4 USB topology")
    require(not args.out.exists(), "X4 evidence directory already exists")
    image = args.image.read_bytes()
    erase_end = validate_image(image, args.sha256)
    require(BACKUP.stat().st_size == 16_777_216 and digest(BACKUP.read_bytes()) == BACKUP_SHA,
            "Verified full X4 recovery backup absent or changed")
    port = port_now(args.location)
    locks = []
    try:
        for key in sorted(("port:" + port, "mac:" + MAC)):
            handle = (LOCK_DIR / (digest(key.encode()) + ".lock")).open("a+")
            try:
                fcntl.flock(handle.fileno(), fcntl.LOCK_EX | fcntl.LOCK_NB)
            except BlockingIOError:
                handle.close()
                raise SystemExit(75)  # Interactive/device owner holds the X4; retry later.
            locks.append(handle)
        args.out.mkdir(mode=0o700, parents=True)
        record = {"schema": 1, "board": "xteink-x4-pro", "mac": MAC,
                  "image_sha256": args.sha256, "image_bytes": len(image),
                  "app_offset": APP_OFFSET, "erase_end_exclusive": erase_end,
                  "result": "failed", "visual_qualified": False, "buttons_qualified": False,
                  "txt_qualified": False}
        stage = "preflight"
        prewrite = None

        def save():
            temp = args.out / "result.tmp"
            temp.write_text(json.dumps(record, indent=2) + "\n")
            temp.replace(args.out / "result.json")

        def run(label, action, *values, timeout=180):
            require(port_now(args.location) == port, "X4 USB port changed")
            command = [sys.executable, "-m", "esptool", "--chip", "esp32s3", "--port", port,
                       "--baud", "115200", "--before", "usb-reset", "--after", "hard-reset",
                       action, *map(str, values)]
            completed = subprocess.run(command, capture_output=True, text=True, timeout=timeout)
            output = completed.stdout + completed.stderr
            (args.out / (label + ".log")).write_text(output[:100_000])
            require(MAC in output.lower() and completed.returncode == 0,
                    "X4 " + label + " failed; see private log")
            return output

        def read(label, address, size):
            path = args.out / (label + ".bin")
            require(not path.exists(), "Stale X4 read file")
            try:
                run(label, "read-flash", hex(address), hex(size), path, timeout=240)
                data = path.read_bytes()
                require(len(data) == size, "X4 " + label + " read length mismatch")
                return data
            finally:
                path.unlink(missing_ok=True)

        def protected(prefix):
            table = read(prefix + "-table", 0x8000, 4096)
            require(digest(table) == TABLE_SHA, "X4 partition table changed")
            ota = read(prefix + "-ota", 0xe000, 8192)
            require(ota_app0(ota), "X4 app0 is not selected")
            return {"bootloader": digest(read(prefix + "-bootloader", 0, 0x8000)),
                    "table": digest(table), "nvs": digest(read(prefix + "-nvs", 0x9000, 0x5000)),
                    "ota": digest(ota)}

        signal.signal(signal.SIGALRM, lambda *_: (_ for _ in ()).throw(TimeoutError("X4 suite deadline")))
        signal.alarm(900)
        try:
            require("Detected flash size: 16MB" in run("flash-id", "flash-id", timeout=60),
                    "X4 flash capacity changed")
            before = protected("before")
            record["protected_before"] = before
            prewrite = read("prewrite-app", APP_OFFSET, erase_end - APP_OFFSET)
            (args.out / "prewrite-app-backup.bin").write_bytes(prewrite)
            record["prewrite_app_sha256"] = digest(prewrite)
            save()
            stage = "writing"
            record["stage"] = stage
            save()
            run("write", "write-flash", "--flash-mode", "keep", "--flash-freq", "keep",
                "--flash-size", "keep", hex(APP_OFFSET), args.image, timeout=400)
            stage = "verifying"
            record["stage"] = stage
            save()
            run("verify", "verify-flash", hex(APP_OFFSET), args.image, timeout=240)
            record["candidate_readback_equal"] = True
            record["protected_after"] = protected("after")
            require(record["protected_after"] == before, "X4 protected regions changed")
            record["protected_equal"] = True
            stage = "boot"
            record["stage"] = stage
            save()
            lines, count = observe(port, args.location)
            record["serial_bytes"] = count
            record["boot"] = validate_boot(lines)
            record["result"] = "pass"
        except BaseException as error:
            record["error"] = f"{type(error).__name__}: {error}"[:300]
            if stage != "preflight" and prewrite is not None:
                try:
                    signal.alarm(600)  # Separate bounded recovery budget.
                    stage = "restoring"
                    record["stage"] = stage
                    save()
                    run("restore", "write-flash", "--flash-mode", "keep", "--flash-freq", "keep",
                        "--flash-size", "keep", hex(APP_OFFSET), args.out / "prewrite-app-backup.bin", timeout=400)
                    run("restore-verify", "verify-flash", hex(APP_OFFSET),
                        args.out / "prewrite-app-backup.bin", timeout=240)
                    record["prewrite_restored"] = True
                    record["protected_after_restore"] = protected("restored")
                    require(record["protected_after_restore"] == before,
                            "X4 protected regions changed during restore")
                    lines, count = observe(port, args.location)
                    record["restored_boot_serial_bytes"] = count
                    record["restored_boot"] = validate_restored_boot(lines)
                except BaseException as restore_error:
                    record["restore_error"] = f"{type(restore_error).__name__}: {restore_error}"[:300]
            save()
            raise
        finally:
            signal.alarm(0)
            save()
        print(json.dumps({"result": record["result"], "image_sha256": args.sha256,
                          "candidate_readback_equal": record.get("candidate_readback_equal"),
                          "protected_equal": record.get("protected_equal"), "boot": record.get("boot")}))
    finally:
        while locks:
            locks.pop().close()


if __name__ == "__main__":
    main()
