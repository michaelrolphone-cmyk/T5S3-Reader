"""No physical adapter is instantiated. All device operations use FakeTransport."""
import copy
import hashlib
import json
from pathlib import Path
import struct
import tempfile
import unittest
from unittest.mock import patch, Mock
import zlib

import trusted_targets as controller
from x4 import adapter, contract as x4
from test_targets import GitHub, IMAGE, SHA, DIGEST


def metadata():
    table = bytearray(b"\xff" * 4096)
    rows = [(1, 2, 0x9000, 0x5000), (1, 0, 0xe000, 0x2000),
            (0, 16, 0x10000, 0x640000), (0, 17, 0x650000, 0x640000),
            (1, 130, 0xc90000, 0x360000), (1, 3, 0xff0000, 0x10000)]
    for i, row in enumerate(rows):
        table[i*32:i*32+32] = struct.pack("<HBBII16sI", 0x50aa, *row, b"fixture", 0)
    ota = bytearray(b"\xff" * 8192)
    struct.pack_into("<I", ota, 0, 1)
    struct.pack_into("<II", ota, 24, 2, zlib.crc32(struct.pack("<I", 1), 0xffffffff))
    return bytes(table), bytes(ota)


TABLE, OTA = metadata()
PROFILE = {"model": x4.TARGET, "mac": x4.MAC, "table_sha256": hashlib.sha256(TABLE).hexdigest()}
USB = {"port": "/dev/cu.usbmodemTEST", "vid": 0x303a, "pid": 0x1001, "serial_number": x4.MAC}


class FakeTransport:
    def __init__(self, fail=None):
        self.fail = fail
        self.writes = []
        self.closed = False
        self.identifications = 0
        self.table, self.ota = TABLE, OTA

    def identify(self):
        self.identifications += 1
        if self.fail == "disconnect_identity":
            raise OSError("private secret")
        mac = "11:22:33:44:55:66" if self.fail == "wrong_mac" else x4.MAC
        if self.fail == "swapped" and self.identifications == 2:
            mac = "11:22:33:44:55:66"
        return x4.CHIP, mac, 0x1000000

    def usb_identity(self):
        return USB

    def read(self, address, size):
        if self.fail == "disconnect_read":
            raise OSError("private secret")
        if address == 0x10000:
            return b"bad" if self.fail == "readback" else IMAGE
        if address == 0x8000:
            return b"bad" if self.writes and self.fail == "metadata" else self.table
        if address == 0xe000:
            return self.ota
        return bytes(size)

    def write_app(self, address, image):
        self.writes.append((address, image))
        if self.fail == "disconnect_write":
            raise OSError("private secret")

    def boot_stream(self):
        if self.fail == "disconnect_boot":
            raise OSError("private secret")
        return object()

    def close(self):
        self.closed = True
        if self.fail == "close":
            raise OSError("private secret")


def observation(_stream, sha, digest):
    return {"target": x4.TARGET, "source_sha": sha, "firmware_sha256": digest,
            "result": "pass", "error": None, "checks": {key: "pass" for key in x4.TESTS}}


def transact(transport, **kwargs):
    return adapter.transaction(transport, USB, PROFILE, IMAGE, SHA, DIGEST, observer=observation, **kwargs)


class AdapterTests(unittest.TestCase):
    def test_complete_transaction_and_simulation_cannot_pass_hardware(self):
        device = FakeTransport()
        result = transact(device)
        self.assertEqual(result["result"], "pass")
        self.assertEqual(device.writes, [(0x10000, IMAGE)])
        self.assertTrue(device.closed)
        self.assertEqual(device.identifications, 2)
        aggregate = {"mode": "hardware", "target": "x4", "source_sha": SHA,
                     "firmware_sha256": DIGEST, "device": result}
        self.assertFalse(controller.hardware_success(aggregate))
        # Only a fixture result in a fake controller run uses hardware mode.
        aggregate["device"]["mode"] = "hardware"
        self.assertTrue(controller.hardware_success(aggregate))
        for key in ("identity_verified", "candidate_readback_equal", "metadata_preserved", "closed"):
            bad = copy.deepcopy(aggregate)
            bad["device"][key] = False
            self.assertFalse(controller.hardware_success(bad))
        bad = copy.deepcopy(aggregate)
        bad["device"]["target"] = "cam"
        self.assertFalse(controller.hardware_success(bad))

    def test_wrong_device_artifact_and_layout_refuse_before_write(self):
        for failure in ("wrong_mac", "swapped", "disconnect_identity", "disconnect_read"):
            device = FakeTransport(failure)
            result = transact(device)
            self.assertEqual(device.writes, [])
            self.assertTrue(device.closed)
            self.assertEqual(result["result"], "failed")
        for image, sha, digest in ((b"CAM", SHA, DIGEST), (IMAGE, "bad", DIGEST),
                                   (IMAGE, SHA, "f" * 64)):
            device = FakeTransport()
            result = adapter.transaction(device, USB, PROFILE, image, sha, digest, observer=observation)
            self.assertEqual(device.identifications, 0)
            self.assertEqual(device.writes, [])
        device = FakeTransport()
        device.table = bytes(4096)
        result = transact(device)
        self.assertEqual(device.writes, [])
        self.assertEqual(result["result"], "failed")
        device = FakeTransport()
        device.ota = bytes(8192)
        result = transact(device)
        self.assertEqual(device.writes, [])

    def test_disconnect_readback_metadata_cleanup_no_automatic_reflash(self):
        for failure in ("disconnect_write", "readback", "metadata", "disconnect_boot", "close"):
            device = FakeTransport(failure)
            result = transact(device)
            self.assertEqual(len(device.writes), 1)
            self.assertTrue(device.closed)
            self.assertEqual(result["result"], "failed")
            self.assertTrue(result["manual_recovery_required"])
            self.assertNotIn("secret", json.dumps(result))

    def test_missing_diagnostics_exact_head_and_deadline_failure(self):
        for field in x4.TESTS:
            def missing(stream, sha, digest):
                out = observation(stream, sha, digest)
                out["checks"][field] = "missing"
                return out
            result = adapter.transaction(FakeTransport(), USB, PROFILE, IMAGE, SHA, DIGEST, observer=missing)
            self.assertEqual(result["result"], "failed")
        def wrong_head(stream, sha, digest):
            return observation(stream, "b" * 40, digest)
        result = adapter.transaction(FakeTransport(), USB, PROFILE, IMAGE, SHA, DIGEST, observer=wrong_head)
        self.assertEqual(result["result"], "failed")
        def timeout(*_):
            raise TimeoutError("secret")
        result = adapter.transaction(FakeTransport(), USB, PROFILE, IMAGE, SHA, DIGEST, observer=timeout)
        self.assertEqual(result["result"], "failed")

    def test_live_gate_precedes_every_external_operation(self):
        with patch.object(adapter, "inventory") as inv, patch.object(adapter, "locks") as lock:
            with self.assertRaisesRegex(RuntimeError, "disabled"):
                adapter.run(Path("/never-open"), SHA, DIGEST, PROFILE)
            inv.assert_not_called()
            lock.assert_not_called()
            with self.assertRaisesRegex(RuntimeError, "disabled"):
                adapter.SerialTransport(USB)
        gh = GitHub()
        with patch.object(controller, "_hardware_once") as core:
            with self.assertRaisesRegex(RuntimeError, "disabled"):
                controller.hardware_once(gh, 350, Path("/never-create"), PROFILE)
            core.assert_not_called()
        self.assertEqual(gh.posts, [])

    def test_actual_adapter_methods_with_fake_esptool_and_serial(self):
        # Construct without __init__ solely to provide fake backends. The live
        # constructor gate is tested separately and never modified or patched.
        transport = object.__new__(adapter.SerialTransport)
        transport.usb = USB
        transport.esp = Mock()
        transport.esp.CHIP_NAME = x4.CHIP
        transport.esp.read_mac.return_value = bytes.fromhex(x4.MAC.replace(":", ""))
        transport.esp.flash_id.return_value = 0x1840c8
        transport.esp.read_flash.return_value = IMAGE
        transport.port = Mock()
        transport.serial = Mock()
        transport.esptool = Mock()
        seen = []
        def write(args, esp):
            self.assertIs(esp, transport.esp)
            self.assertIn("write_flash", args)
            self.assertEqual(args[-2], "0x10000")
            self.assertEqual(Path(args[-1]).read_bytes(), IMAGE)
            self.assertNotIn("--erase-all", args)
            seen.append(args[-1])
        transport.esptool.main.side_effect = write
        with patch.object(adapter, "inventory", return_value=[USB]):
            self.assertEqual(transport.identify(), (x4.CHIP, x4.MAC, 0x1000000))
            self.assertEqual(transport.read(0x10000, len(IMAGE)), IMAGE)
            with self.assertRaises(ValueError):
                transport.read(0, 4096)
            with self.assertRaises(ValueError):
                transport.write_app(0, IMAGE)
            transport.write_app(0x10000, IMAGE)
            self.assertFalse(Path(seen[0]).exists())
            port = transport.boot_stream()
            transport.serial.Serial.assert_called_once_with(port=None, baudrate=115200, timeout=1,
                                                            write_timeout=10, exclusive=True)
            self.assertFalse(port.dtr)
            self.assertFalse(port.rts)
            port.open.assert_called_once()
            transport.close()
            port.close.assert_called_once()
        with patch.object(adapter, "inventory", return_value=[dict(USB, serial_number="wrong")]):
            with self.assertRaises(ValueError):
                transport.write_app(0x10000, IMAGE)
        self.assertEqual(transport.esptool.main.call_count, 1)

    def test_multi_target_scheduler_keeps_cam_and_x4_disabled(self):
        with tempfile.TemporaryDirectory() as temp, patch.object(controller.cam, "once") as cam:
            with patch.object(controller, "_hardware_once") as x4_runner:
                out = controller.hardware_scan(GitHub(), [350], Path(temp), Path("unused"), {}, PROFILE,
                                               targets=("x4", "cam"))
                self.assertEqual(out[0], {"target": "x4", "result": "blocked_or_failed"})
                self.assertEqual(out[1]["target"], "cam")
                cam.assert_called_once()
                x4_runner.assert_not_called()
            with patch.object(controller, "hardware_once", return_value={"status_state": "success"}):
                out = controller.hardware_scan(GitHub(), [350], Path(temp), Path("unused"), {}, PROFILE,
                                               targets=("x4",))
                self.assertEqual(out, [{"target": "x4", "result": "success"}])

    def test_hardware_wrong_artifact_reports_without_runner(self):
        gh = GitHub()
        gh.blob = b"bad artifact"
        with tempfile.TemporaryDirectory() as temp:
            runner = Mock()
            result = controller._hardware_once(gh, 350, Path(temp), PROFILE, runner)
            runner.assert_not_called()
            self.assertEqual(result["status_state"], "error")
            self.assertEqual([p[1]["state"] for p in gh.posts], ["error"])

    def test_binding_and_unique_descriptor(self):
        self.assertEqual(adapter.locate([USB]), USB)
        for rows in ([], [USB, USB], [dict(USB, serial_number="wrong")], [dict(USB, port="/dev/cu.usbserial-1")]):
            with self.assertRaises(ValueError):
                adapter.locate(rows)
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / "binding.json"
            path.write_text(json.dumps(PROFILE))
            path.chmod(0o600)
            self.assertEqual(adapter.binding(path), PROFILE)
            path.chmod(0o644)
            with self.assertRaises(ValueError):
                adapter.binding(path)

    def test_hardware_journal_status_retry_never_reflashes(self):
        class StatusGitHub(GitHub):
            def call(self, path, method="GET", body=None, limit=1000000):
                if method == "POST" and body.get("state") != "pending" and self.fail_status:
                    raise OSError("private")
                old = self.fail_status
                self.fail_status = False
                try:
                    return super().call(path, method, body, limit)
                finally:
                    self.fail_status = old
        gh = StatusGitHub()
        calls = []
        def runner(path, sha, digest, profile):
            calls.append(path)
            return transact(FakeTransport(), mode="hardware")
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            gh.fail_status = True
            with self.assertRaises(OSError):
                controller._hardware_once(gh, 350, root, PROFILE, runner)
            gh.fail_status = False
            controller._hardware_once(gh, 350, root, PROFILE, runner)
            controller._hardware_once(gh, 350, root, PROFILE, runner)
            self.assertEqual(len(calls), 1)
            self.assertEqual([p[1]["state"] for p in gh.posts], ["pending", "success"])
            self.assertEqual({p[1]["context"] for p in gh.posts}, {"X4 hardware / trusted owner SHA"})
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            (root / "hardware/x4" / SHA).mkdir(parents=True)
            with self.assertRaisesRegex(RuntimeError, "manual recovery"):
                controller._hardware_once(gh, 350, root, PROFILE, runner)
            self.assertEqual(len(calls), 1)


if __name__ == "__main__":
    unittest.main()
