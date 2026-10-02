#!/usr/bin/env python3
"""Bounded CAM-only pilot, adapted from the verified camera-app-flash-2 procedure.

No relay, RF, buzzer, T5S3, image transfer, raw serial log, or power-cut claim.
Flash is transactional against the exact known camera utility image, with a
private local backup and mandatory restoration/readback in finally.
"""
import argparse
import fcntl
import hashlib
import json
import re
import signal
import struct
import sys
import time
from pathlib import Path

PORT = "/dev/cu.usbserial-2340"
LOCATION = "2-3.4"
MAC = "28:84:85:4b:57:98"
BASELINE_SHA = "e208d9baafc1f8bd9d18eb4b659648e082b44381978d178cc8a44be189515e6d"
BASELINE_LENGTH = 541328
BACKUP_LENGTH = 561152
LOCK_DIR = Path("/Users/micahelbyrns/Documents/Codex/2026-09-30/task/.device-locks")


def require(ok, why):
    if not ok:
        raise RuntimeError(why)


class DeviceLocks:
    """Same lock keys and directory as the proven CAM lab scripts."""
    def __init__(self):
        self.handles = []

    def __enter__(self):
        LOCK_DIR.mkdir(parents=True, exist_ok=True)
        try:
            for key in sorted(("port:" + PORT, "mac:" + MAC)):
                path = LOCK_DIR / (hashlib.sha256(key.encode()).hexdigest() + ".lock")
                handle = path.open("a+")
                try:
                    fcntl.flock(handle.fileno(), fcntl.LOCK_EX | fcntl.LOCK_NB)
                except BlockingIOError:
                    handle.close()
                    raise RuntimeError("CAM owned by another lab process")
                self.handles.append(handle)
            return self
        except BaseException:
            self.__exit__(None, None, None)
            raise

    def __exit__(self, *_):
        while self.handles:
            self.handles.pop().close()


def inventory():
    from serial.tools import list_ports
    return sorted((p.device, p.location, p.vid, p.pid) for p in list_ports.comports())


def verify(esp):
    require(esp.CHIP_NAME == "ESP32-S3", "CAM chip identity mismatch")
    require(":".join(f"{x:02x}" for x in esp.read_mac()) == MAC, "CAM MAC mismatch")


def partition(table, image_size):
    require(len(table) == 4096, "CAM partition table short")
    apps = []
    ranges = []
    for pos in range(0, 4096, 32):
        row = table[pos:pos + 32]
        if row[:2] != b"\xaaP":
            require(row[:2] in (b"\xff\xff", b"\xeb\xeb"), "CAM partition table malformed")
            break
        _, kind, subtype, offset, size, _, flags = struct.unpack("<HBBII16sI", row)
        require(offset >= 0x9000 and size > 0 and offset + size <= 0x1000000 and flags == 0,
                "CAM partition out of bounds")
        ranges.append((offset, offset + size))
        if kind == 0:
            apps.append((subtype, offset, size))
    ranges.sort()
    require(all(a[1] <= b[0] for a, b in zip(ranges, ranges[1:])), "CAM partitions overlap")
    require(len(apps) == 1 and apps[0][0] == 0 and apps[0][1] == 0x10000
            and apps[0][2] == 0x300000 and 0 < image_size <= 1024 * 1024,
            "Unqualified CAM app layout or image size")
    return apps[0][1]


def write_flash(esptool, esp, image_path):
    esptool.main(["--chip", "esp32s3", "--port", PORT, "--baud", "115200",
        "--no-stub", "--after", "no_reset_stub", "write_flash", "--compress",
        "--flash_mode", "keep", "--flash_freq", "keep", "--flash_size", "keep",
        "0x10000", str(image_path)], esp=esp)


def observe(port, seconds):
    # Only known, short diagnostic lines are retained. Arbitrary raw bytes,
    # CAMERA_PRIVATE image transfer lines and SD content never leave the port.
    deadline = time.monotonic() + seconds
    pending = bytearray()
    lines = []
    received = 0
    while time.monotonic() < deadline and received < 768 * 1024 and len(lines) < 600:
        data = port.read(512)
        received += len(data)
        pending.extend(data)
        while b"\n" in pending:
            row, _, rest = pending.partition(b"\n")
            pending = bytearray(rest)
            line = row.decode("utf-8", "replace").rstrip("\r")
            if len(line) <= 512 and line.startswith(("RUNTIME ", "CAMERA_APP ")):
                lines.append(line)
        require(len(pending) <= 4096, "CAM serial line bound exceeded")
        if any("Guru Meditation" in x or "result=failed" in x for x in lines):
            break
        if any("RUNTIME APP default returned artifact=camera_utility.elf result=0" in x for x in lines):
            if sum(x.startswith("RUNTIME BOOT heartbeat state=Running ") for x in lines) >= 3:
                break
    return lines


def validate(lines):
    require(any("mac=" + MAC + " " in x for x in lines), "CAM boot MAC absent")
    require(sum("profile=cam-offline" in x for x in lines) == 1, "CAM unexpected reboot")
    require(sum("RUNTIME BOOT state=Running" in x for x in lines) == 1, "CAM boot not Running")
    require(any("RUNTIME APP default returned artifact=camera_utility.elf result=0" in x for x in lines),
            "CAM utility failed or did not return")
    require(not any("result=failed" in x or "Guru Meditation" in x or "Backtrace:" in x for x in lines),
            "CAM runtime failure")
    saved = [re.fullmatch(r"CAMERA_APP saved=/sd/camera-utility-\d{4}\.jpg bytes=(\d+)", x)
             for x in lines]
    sizes = [int(m[1]) for m in saved if m]
    require(len(sizes) == 1 and 4 <= sizes[0] <= 96 * 1024, "CAM saved image count/size invalid")
    heartbeats = sum(x.startswith("RUNTIME BOOT heartbeat state=Running ") for x in lines)
    require(heartbeats >= 3, "CAM steady heartbeat missing")
    return {"image_bytes": sizes[0], "post_pass_heartbeats": heartbeats, "image_private": True}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--image", type=Path, required=True)
    parser.add_argument("--sha256", required=True)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    require(not args.out.exists(), "Evidence directory already exists")
    image = args.image.read_bytes()
    require(re.fullmatch(r"[0-9a-f]{64}", args.sha256) is not None
            and hashlib.sha256(image).hexdigest() == args.sha256, "Candidate SHA-256 mismatch")
    args.out.mkdir(mode=0o700, parents=True)
    result = {"board": "cam", "mac": MAC, "candidate_sha256": args.sha256, "result": "failed"}
    signal.signal(signal.SIGALRM, lambda *_: (_ for _ in ()).throw(TimeoutError("CAM deadline")))
    signal.alarm(540)
    esp = None
    backup = None
    try:
        with DeviceLocks():
            before = inventory()
            require(before.count((PORT, LOCATION, 0x1a86, 0x7523)) == 1, "CAM USB identity mismatch")
            import serial
            import esptool
            port = serial.Serial(PORT, 115200, timeout=1, exclusive=True)
            try:
                esp = esptool.detect_chip(port, 115200, connect_attempts=1)
                verify(esp)
                esp = esp.run_stub()
                esp.flash_spi_attach(0)
                require(1 << ((esp.flash_id() >> 16) & 255) == 16 * 1024 * 1024,
                        "CAM flash capacity mismatch")
                table = esp.read_flash(0x8000, 4096)
                offset = partition(table, len(image))
                require(offset == 0x10000, "CAM app offset mismatch")
                nvs_digest = hashlib.sha256(esp.read_flash(0x9000, 0x5000)).hexdigest()
                backup = esp.read_flash(offset, BACKUP_LENGTH)
                require(hashlib.sha256(backup[:BASELINE_LENGTH]).hexdigest() == BASELINE_SHA,
                        "Installed CAM image differs from verified checkpoint")
                result["baseline_sha256"] = BASELINE_SHA
                result["backup_sha256"] = hashlib.sha256(backup).hexdigest()
                private_backup = args.out / "prewrite-app-backup.bin"
                private_backup.write_bytes(backup)
                require(inventory() == before, "CAM USB inventory changed before flash")
                verify(esp)
                try:
                    write_flash(esptool, esp, args.image)
                    require(esp.read_flash(offset, len(image)) == image, "CAM candidate readback mismatch")
                    require(esp.read_flash(0x8000, 4096) == table, "CAM partition table changed")
                    require(hashlib.sha256(esp.read_flash(0x9000, 0x5000)).hexdigest() == nvs_digest,
                            "CAM NVS changed")
                    result["candidate_readback_equal"] = True
                    esp.hard_reset()
                    lines = observe(port, 180)
                    result.update(validate(lines))
                    result["result"] = "pass"
                finally:
                    # Restore the exact private prewrite bytes even after a test
                    # failure. A restoration failure invalidates the result.
                    port.close()
                    with serial.Serial(PORT, 115200, timeout=1, exclusive=True) as restore_port:
                        restore_esp = esptool.detect_chip(restore_port, 115200, connect_attempts=1)
                        verify(restore_esp)
                        restore_esp = restore_esp.run_stub()
                        restore_esp.flash_spi_attach(0)
                        write_flash(esptool, restore_esp, private_backup)
                        require(restore_esp.read_flash(offset, BACKUP_LENGTH) == backup,
                                "CAM baseline restoration readback failed")
                        require(restore_esp.read_flash(0x8000, 4096) == table,
                                "CAM partition changed on restore")
                        require(hashlib.sha256(restore_esp.read_flash(0x9000, 0x5000)).hexdigest() == nvs_digest,
                                "CAM NVS changed on restore")
                        result["baseline_restored"] = True
                        restore_esp.hard_reset()
            finally:
                if port.is_open:
                    port.close()
    except Exception as exc:
        result["result"] = "failed"
        result["error"] = f"{type(exc).__name__}: {exc}"[:300]
        raise
    finally:
        (args.out / "result.json").write_text(json.dumps(result, indent=2) + "\n")
        signal.alarm(0)


if __name__ == "__main__":
    main()
