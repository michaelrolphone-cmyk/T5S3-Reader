#!/usr/bin/env python3
"""Explicit, bounded Mac lab operations; no port opening during inventory.

Use a Python with installed pyserial. For identify/flash/capture, pass the
installed esptool directory. These operations reset the selected board.
"""
import argparse
import hashlib
import json
import re
import signal
import struct
import subprocess
import sys
import time
from pathlib import Path


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def inventory():
    from serial.tools import list_ports
    return sorted([
        {"port": p.device, "vid": p.vid, "pid": p.pid,
         "location": p.location, "product": p.product}
        for p in list_ports.comports() if p.vid is not None
    ], key=lambda p: p["port"])


def usb_devices():
    # Read registry metadata, never device files. Return only USB descriptors,
    # not the unrelated root registry properties emitted by ioreg -l.
    result = subprocess.run(["/usr/sbin/ioreg", "-p", "IOUSB", "-l", "-w", "0"],
                            capture_output=True, text=True, timeout=10, check=True)
    devices, current = [], None
    for line in result.stdout.splitlines():
        if "+-o " in line:
            current = None
            if "<class IOUSBHostDevice," in line:
                current = {"name": line.split("+-o ", 1)[1].split("  <", 1)[0]}
                devices.append(current)
        if current is not None:
            match = re.search(r'"(idVendor|idProduct|locationID)" = (\d+)', line)
            if match:
                current[match[1]] = int(match[2])
    return devices


def factory_region(table, image_size):
    """Reject layouts we have not qualified; never overwrite OTA/NVS by guess."""
    require(len(table) == 4096, "Incomplete partition table")
    parts = []
    for pos in range(0, 4096, 32):
        record = table[pos:pos + 32]
        if record[:2] != b"\xaaP":
            require(record[:2] in (b"\xeb\xeb", b"\xff\xff"), "Malformed partition record")
            break
        _, kind, subtype, offset, size, _, flags = struct.unpack("<HBBII16sI", record)
        require(size > 0 and offset >= 0x9000 and offset + size <= 0x1000000,
                "Partition outside verified 16 MB flash")
        require(not flags, "Partition flags require a separately reviewed flow")
        parts.append((kind, subtype, offset, size))
    ordered = sorted(parts, key=lambda p: p[2])
    require(all(a[2] + a[3] <= b[2] for a, b in zip(ordered, ordered[1:])),
            "Overlapping partitions")
    apps = [p for p in parts if p[0] == 0]
    require(len(apps) == 1 and apps[0][1] == 0 and apps[0][2] == 0x10000,
            "Only the verified single-factory-app layout is supported")
    erased_size = (image_size + 4095) // 4096 * 4096
    require(0 < image_size <= 2 * 1024 * 1024 and erased_size <= apps[0][3],
            "Image or erase range exceeds factory partition")
    return apps[0][2]


def verify_target(esp, expected_mac):
    mac = ":".join(f"{n:02x}" for n in esp.read_mac())
    require(esp.CHIP_NAME == "ESP32-S3", "Unexpected chip; no write")
    require(mac == expected_mac.lower(), "MAC mismatch; no write")
    return mac


def capture(esp, seconds):
    esp._port.timeout = 0.2
    esp.hard_reset()
    deadline = time.monotonic() + seconds
    data = bytearray()
    while time.monotonic() < deadline and len(data) < 65536:
        data.extend(esp._port.read(min(2048, 65536 - len(data))))
    # Only this checked-in image's structured diagnostic lines are retained.
    # Never save arbitrary existing firmware logs or NVS contents/credentials.
    return re.findall(r"RTE_CORE_[^\r\n]*", data.decode("utf-8", "replace"))


def timeout_handler(signum, frame):
    raise TimeoutError("Lab operation exceeded 180 seconds; no automatic retry")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("action", choices=["inventory", "identify", "capture-core", "flash-core"])
    parser.add_argument("--port")
    parser.add_argument("--mac", help="Required exact chip MAC; never choose a port by order")
    parser.add_argument("--location", help="Expected current USB location from inventory")
    parser.add_argument("--esptool-dir", type=Path)
    parser.add_argument("--out", type=Path, help="New evidence directory; existing directories are refused")
    parser.add_argument("--image", type=Path)
    parser.add_argument("--sha256")
    parser.add_argument("--revision", help="Expected 40-digit source commit in core boot output")
    args = parser.parse_args()
    signal.signal(signal.SIGALRM, timeout_handler)
    signal.alarm(180)
    if args.action == "inventory":
        print(json.dumps({"usb_devices": usb_devices(), "serial_interfaces": inventory()}, indent=2))
        return
    require(args.port and args.esptool_dir and args.out, "port, esptool-dir and out are required")
    require(not args.out.exists(), "Evidence directory exists; choose a new path")
    require((args.esptool_dir / "esptool/__init__.py").is_file(), "Installed esptool package missing")
    before = inventory()
    selected = [p for p in before if p["port"] == args.port]
    require(len(selected) == 1, "Selected port not uniquely enumerated")
    if args.location:
        require(selected[0]["location"] == args.location, "USB location changed")
    if args.action != "identify":
        require(args.mac and re.fullmatch(r"(?:[0-9a-fA-F]{2}:){5}[0-9a-fA-F]{2}", args.mac), "Exact MAC required")
    image = None
    if args.action == "flash-core":
        require(args.location and args.image and args.sha256 and args.revision,
                "flash-core requires location, image, sha256 and revision")
        require((selected[0]["vid"], selected[0]["pid"]) == (0x1a86, 0x7523),
                "This flash flow is qualified only for the CAM's USB-UART interface")
        require(re.fullmatch(r"[0-9a-f]{40}", args.revision), "Expected exact source commit required")
        require(args.image.stat().st_size <= 2 * 1024 * 1024, "Image too large")
        image = args.image.read_bytes()
        require(hashlib.sha256(image).hexdigest() == args.sha256, "Image SHA mismatch")
    args.out.mkdir(parents=True)
    report = {"action": args.action, "inventory_before": before, "target": selected[0]}
    sys.path.insert(0, str(args.esptool_dir.resolve()))
    import esptool
    esp = None
    try:
        esp = esptool.detect_chip(args.port, 115200, connect_attempts=1)
        report["chip"] = esp.CHIP_NAME
        report["mac"] = ":".join(f"{n:02x}" for n in esp.read_mac())
        print("IDENTITY", report["chip"], report["mac"], flush=True)
        if args.action == "identify":
            esp.hard_reset()
        else:
            verify_target(esp, args.mac)
            if args.action == "flash-core":
                esp = esp.run_stub()
                esp.flash_spi_attach(0)
                require((esp.flash_id() >> 16) & 255 == 0x18, "Not verified 16 MB CAM flash")
                table = esp.read_flash(0x8000, 4096)
                address = factory_region(table, len(image))
                require(inventory() == before, "USB inventory changed before write")
                verify_target(esp, args.mac)
                # Freeze the verified bytes in a new task-local evidence file;
                # do not reopen a mutable build output between hashing and flash.
                frozen = args.out / "firmware.bin"
                frozen.write_bytes(image)
                esptool.main(["--chip", "esp32s3", "--port", args.port, "--baud", "115200",
                    "--no-stub", "--after", "no_reset_stub", "write_flash", "--compress",
                    "--flash_mode", "keep", "--flash_freq", "keep", "--flash_size", "keep",
                    hex(address), str(frozen)], esp=esp)
                require(esp.read_flash(address, len(image)) == image, "Flash readback mismatch")
                require(esp.read_flash(0x8000, 4096) == table, "Partition table changed")
                report.update(image_sha256=args.sha256, revision=args.revision,
                              app_offset=address, readback_equal=True, partition_table_unchanged=True)
            lines = capture(esp, 22)
            (args.out / "serial.txt").write_text("\n".join(lines) + "\n")
            for line in lines:
                print(line, flush=True)
            report["boot_lines"] = lines
            require("RTE_CORE_MAC " + args.mac.lower() in lines, "Running MAC not observed")
            require("RTE_CORE_CHECK streams_context_cleanup=PASS devices=0" in lines,
                    "Core boot check did not pass")
            require(sum(line.startswith("RTE_CORE_IDLE state=ready ") for line in lines) >= 3,
                    "Insufficient healthy idle observations")
            if args.revision:
                require(any("revision=" + args.revision + " " in line for line in lines),
                        "Running source revision mismatch")
        report["result"] = "pass"
    except Exception as exc:
        report.update(result="failed", error=f"{type(exc).__name__}: {exc}")
        raise
    finally:
        if esp is not None:
            esp._port.close()
        (args.out / "result.json").write_text(json.dumps(report, indent=2) + "\n")
        signal.alarm(0)


if __name__ == "__main__":
    main()
