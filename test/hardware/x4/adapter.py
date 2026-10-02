"""Pinned X4 app-only transaction; public live entry remains execution-disabled.

Only fake transports call the transaction in tests. No CLI/config/environment
switch enables this adapter. The gate is checked before inventory/port access.
"""
import contextlib
import hashlib
import json
import os
from pathlib import Path
import re
import signal
import struct
import tempfile
import time
import zlib

from device_locks import locks
from . import contract as x4

REGIONS = (("table", 0x8000, 4096), ("nvs", 0x9000, 0x5000), ("ota", 0xe000, 0x2000))
APP_OFFSET = 0x10000
TOTAL_SECONDS = 600


def binding(path):
    x4.require(not path.is_symlink() and path.is_file() and path.stat().st_mode & 0o077 == 0
               and path.stat().st_size <= 2048, "X4 binding must be private and bounded")
    data = json.loads(path.read_text())
    x4.require(set(data) == {"model", "mac", "table_sha256"}
               and data["model"] == x4.TARGET and data["mac"].lower() == x4.MAC
               and re.fullmatch(r"[0-9a-f]{64}", data["table_sha256"]), "Invalid X4 binding")
    return data


def locate(ports):
    x4.require(len(ports) <= 64, "USB inventory exceeds bound")
    matches = [p for p in ports if p.get("vid") == 0x303a and p.get("pid") == 0x1001
               and str(p.get("serial_number", "")).lower() == x4.MAC]
    x4.require(len(matches) == 1, "X4 USB identity absent or ambiguous")
    item = matches[0]
    x4.require(re.fullmatch(r"/dev/cu\.usbmodem[A-Za-z0-9_-]+", item.get("port", "")),
               "X4 native USB locator invalid")
    return item


def validate_layout(table, ota, image_size, expected_table_sha):
    x4.require(len(table) == 4096 and len(ota) == 8192 and
               hashlib.sha256(table).hexdigest() == expected_table_sha, "X4 partition binding mismatch")
    rows = []
    for pos in range(0, 4096, 32):
        row = table[pos:pos+32]
        if row[:2] != b"\xaaP":
            x4.require(row[:2] in (b"\xeb\xeb", b"\xff\xff"), "X4 partition table malformed")
            break
        _, kind, subtype, offset, size, _, flags = struct.unpack("<HBBII16sI", row)
        x4.require(flags == 0 and size > 0 and offset >= 0x9000
                   and offset + size <= 0x1000000, "X4 partition bounds invalid")
        rows.append((kind, subtype, offset, size))
    expected = [(1, 2, 0x9000, 0x5000), (1, 0, 0xe000, 0x2000),
                (0, 16, APP_OFFSET, x4.MAX_IMAGE), (0, 17, 0x650000, x4.MAX_IMAGE),
                (1, 130, 0xc90000, 0x360000), (1, 3, 0xff0000, 0x10000)]
    x4.require(rows == expected and 0 < image_size <= x4.MAX_IMAGE
               and ((image_size + 4095) // 4096) * 4096 <= x4.MAX_IMAGE,
               "X4 unqualified app layout; never rewrite partitions")
    active = []
    for slot in (0, 1):
        entry = ota[slot*4096:slot*4096+32]
        seq = struct.unpack_from("<I", entry)[0]
        state, crc = struct.unpack_from("<II", entry, 24)
        if seq != 0xffffffff and state not in (3, 4) and crc == zlib.crc32(struct.pack("<I", seq), 0xffffffff):
            active.append(seq)
    x4.require(active and (max(active)-1) % 2 == 0, "X4 app0 not selected; no OTA metadata rewrite")


def transaction(transport, usb, profile, image, sha, digest, *, mode="simulation", observer=x4.observe):
    """Complete bounded transaction over the transport interface, under caller's lock.

    Transport must time-bound each call; live run additionally enforces SIGALRM.
    The only write is the validated app0 image. No automatic destructive retry,
    partition alteration, backup requirement or baseline restoration for X4.
    """
    result = {"target": x4.TARGET, "mac": x4.MAC, "source_sha": sha,
              "firmware_sha256": digest, "mode": mode, "result": "failed",
              "checks": {key: "not_run" for key in x4.TESTS}, "write_started": False,
              "candidate_readback_equal": False, "metadata_preserved": False,
              "identity_verified": False, "stage": "artifact"}
    try:
        x4.require(mode in ("simulation", "hardware") and x4.SHA.fullmatch(sha)
                   and x4.image_digest(image) == digest, "X4 candidate identity invalid")
        result["stage"] = "identity"
        chip, mac, capacity = transport.identify()
        x4.identity(usb, chip, mac, profile["model"])
        x4.require(profile["mac"].lower() == x4.MAC and capacity == 0x1000000,
                   "X4 flash capacity or binding mismatch")
        result["identity_verified"] = True
        result["stage"] = "preflight"
        before = {name: transport.read(address, size) for name, address, size in REGIONS}
        x4.require(all(len(before[name]) == size for name, _, size in REGIONS), "X4 short metadata read")
        validate_layout(before["table"], before["ota"], len(image), profile["table_sha256"])
        # Recheck ROM identity and descriptor immediately before the sole write.
        chip, mac, capacity = transport.identify()
        x4.identity(transport.usb_identity(), chip, mac, profile["model"])
        x4.require(capacity == 0x1000000, "X4 flash capacity changed")
        result["stage"] = "write"
        result["write_started"] = True
        transport.write_app(APP_OFFSET, image)
        result["stage"] = "readback"
        x4.require(transport.read(APP_OFFSET, len(image)) == image, "X4 candidate readback mismatch")
        result["candidate_readback_equal"] = True
        result["stage"] = "metadata"
        x4.require(all(transport.read(address, size) == before[name] for name, address, size in REGIONS),
                   "X4 non-app metadata changed")
        result["metadata_preserved"] = True
        result["stage"] = "boot"
        stream = transport.boot_stream()
        observation = observer(stream, sha, digest)
        x4.require(observation["target"] == x4.TARGET and observation["source_sha"] == sha
                   and observation["firmware_sha256"] == digest, "X4 diagnostic provenance mismatch")
        result["checks"] = {key: observation["checks"][key] for key in x4.TESTS}
        x4.require(observation["result"] == "pass" and observation["error"] is None
                   and all(value == "pass" for value in result["checks"].values()), "X4 diagnostics incomplete")
        result["result"] = "pass"
        result["stage"] = "complete"
    except Exception:
        result["result"] = "failed"
        result["error"] = "transaction_failed"  # no raw exception/serial/private data
    finally:
        try:
            transport.close()
            result["closed"] = True
        except Exception:
            result.update(result="failed", closed=False, error="close_failed")
        result["manual_recovery_required"] = result["write_started"] and result["result"] != "pass"
    return result


def inventory():
    from serial.tools import list_ports
    return [{"port": p.device, "vid": p.vid, "pid": p.pid, "serial_number": p.serial_number}
            for p in list_ports.comports()]


def serial_port(serial_module, name):
    # Acquire pySerial exclusivity before configuring control lines. Opening
    # the runtime console must not implicitly assert reset/boot controls.
    port = serial_module.Serial(port=None, baudrate=115200, timeout=1,
                                write_timeout=10, exclusive=True)
    port.dtr = False
    port.rts = False
    port.port = name
    try:
        port.open()
        return port
    except BaseException:
        port.close()
        raise


class SerialTransport:
    """esptool 4.5.1 / pyserial 3.5 API, same pinned API as the CAM pilot.

    Live construction remains gated. All operations execute under shared locks,
    serial exclusivity and the outer 600-second hard process deadline.
    """
    def __init__(self, usb):
        x4.live()  # unresolved execution approval: before imports or port open
        import serial
        import esptool
        self.serial, self.esptool, self.usb = serial, esptool, usb
        self.port = None
        self.esp = None
        self.port = serial_port(serial, usb["port"])
        try:
            self.esp = esptool.detect_chip(self.port, 115200, connect_attempts=1)
            x4.identity(self.usb_identity(), self.esp.CHIP_NAME,
                        ":".join(f"{x:02x}" for x in self.esp.read_mac()), x4.TARGET)
            self.esp = self.esp.run_stub()
            self.esp.flash_spi_attach(0)
        except BaseException:
            self.port.close()
            raise

    def usb_identity(self):
        current = locate(inventory())
        x4.require(current == self.usb, "X4 disconnected or USB locator changed")
        return current

    def identify(self):
        self.usb_identity()
        return (self.esp.CHIP_NAME, ":".join(f"{x:02x}" for x in self.esp.read_mac()),
                1 << ((self.esp.flash_id() >> 16) & 255))

    def read(self, address, size):
        self.usb_identity()
        x4.require((address, size) in {(a, s) for _, a, s in REGIONS}
                   or (address == APP_OFFSET and 0 < size <= x4.MAX_IMAGE), "X4 read outside allowed regions")
        return self.esp.read_flash(address, size)

    def write_app(self, address, image):
        self.usb_identity()
        x4.require(address == APP_OFFSET, "X4 app-only write required")
        x4.image_digest(image)
        with tempfile.NamedTemporaryFile(prefix="x4-accepted-", suffix=".bin") as frozen:
            frozen.write(image)
            frozen.flush()
            self.esptool.main(["--chip", "esp32s3", "--port", self.usb["port"], "--baud", "115200",
                "--no-stub", "--after", "no_reset_stub", "write_flash", "--compress",
                "--flash_mode", "keep", "--flash_freq", "keep", "--flash_size", "keep",
                "0x10000", frozen.name], esp=self.esp)

    def boot_stream(self):
        self.esp.hard_reset()
        self.port.close()
        deadline = time.monotonic() + 10
        while time.monotonic() < deadline:
            try:
                self.usb_identity()
                self.port = serial_port(self.serial, self.usb["port"])
                return self.port
            except (OSError, ValueError):
                time.sleep(0.25)
        raise TimeoutError("X4 boot USB reconnect timeout")

    def close(self):
        if self.port is not None:
            self.port.close()


def run(image_path, sha, digest, profile):
    x4.live()  # No supported activation flag/config/env; denied before inventory.
    x4.require(image_path.stat().st_size <= x4.MAX_IMAGE, "X4 image exceeds bound")
    image = image_path.read_bytes()
    x4.require(x4.image_digest(image) == digest, "X4 digest mismatch")
    usb = locate(inventory())
    previous = signal.signal(signal.SIGALRM, lambda *_: (_ for _ in ()).throw(TimeoutError("X4 deadline")))
    signal.alarm(TOTAL_SECONDS)
    try:
        with locks(usb["port"], x4.MAC):
            # Suppress esptool progress/serial/exception payloads. Persist only
            # transaction's fixed-schema result, never raw transport output.
            with open(os.devnull, "w") as sink, contextlib.redirect_stdout(sink), contextlib.redirect_stderr(sink):
                transport = SerialTransport(usb)
                return transaction(transport, usb, profile, image, sha, digest, mode="hardware")
    finally:
        signal.alarm(0)
        signal.signal(signal.SIGALRM, previous)
