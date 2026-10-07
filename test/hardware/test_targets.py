import hashlib
import io
import json
import multiprocessing
from pathlib import Path
import struct
import tempfile
import unittest
from unittest.mock import patch
import zipfile

import trusted_targets as controller
from x4 import contract as x4

SHA = "a" * 40
IMAGE = bytearray(24)
IMAGE[0] = 0xe9
struct.pack_into("<H", IMAGE, 12, 9)
IMAGE = bytes(IMAGE) + x4.MARKER + b"\0"
DIGEST = hashlib.sha256(IMAGE).hexdigest()


def archive(info=None, extra=None):
    data = io.BytesIO()
    with zipfile.ZipFile(data, "w") as z:
        z.writestr(x4.IMAGE, IMAGE)
        z.writestr("manifest.json", json.dumps(info or x4.manifest(IMAGE, SHA, 7, 1)))
        if extra:
            z.writestr(extra, "private image bytes")
    return data.getvalue()


class GitHub:
    def __init__(self):
        repo = {"id": 42, "full_name": controller.cam.REPO}
        self.pr = {"number": 350, "state": "open", "user": {"login": controller.cam.OWNER},
                   "head": {"sha": SHA, "repo": repo}, "base": {"repo": repo}}
        self.run = {"id": 7, "run_attempt": 1, "event": "pull_request", "head_sha": SHA,
                    "workflow_id": 8, "repository": repo, "head_repository": repo,
                    "actor": {"login": controller.cam.OWNER}, "pull_requests": [self.pr],
                    "status": "completed", "conclusion": "success"}
        self.artifact = {"id": 9, "name": x4.PREFIX + SHA, "expired": False, "size_in_bytes": 1024}
        self.blob = archive()
        self.posts = []
        self.downloads = 0
        self.fail_status = False
        self.fail_download = False

    def call(self, path, method="GET", body=None, limit=1_000_000):
        if path == "":
            return self.pr["base"]["repo"]
        if path.startswith("/pulls/"):
            return self.pr
        if path == "/actions/workflows/" + x4.WORKFLOW:
            return {"id": 8, "path": ".github/workflows/" + x4.WORKFLOW}
        if "/runs?" in path:
            return {"total_count": 1, "workflow_runs": [self.run]}
        if path.startswith("/actions/runs/"):
            return {"total_count": 1, "artifacts": [self.artifact]}
        if path.endswith("/zip"):
            self.downloads += 1
            if self.fail_download:
                raise OSError("secret transport detail")
            return self.blob
        if path.startswith("/statuses/"):
            if self.fail_status:
                raise OSError("secret status detail")
            self.posts.append((path, body))
            return {"id": len(self.posts)}
        raise AssertionError(path)


def competing_lock(directory, port, mac, queue):
    try:
        with controller.locks(port, mac, Path(directory)):
            queue.put("acquired")
    except BlockingIOError:
        queue.put("blocked")


class TargetTests(unittest.TestCase):
    def test_real_process_shared_lock_mac_alias_cleanup(self):
        with tempfile.TemporaryDirectory() as temp:
            ctx = multiprocessing.get_context("spawn")
            with controller.locks("/dev/cu.usbmodem1", x4.MAC, Path(temp)):
                for port, mac in (("/dev/tty.usbmodem1", "different"),
                                  ("/dev/cu.usbmodem2", x4.MAC.upper())):
                    q = ctx.Queue()
                    p = ctx.Process(target=competing_lock, args=(temp, port, mac, q))
                    p.start()
                    self.assertEqual(q.get(timeout=10), "blocked")
                    p.join(10)
                    self.assertEqual(p.exitcode, 0)
                with controller.locks("/dev/cu.usbserial-1", controller.cam_device.MAC, Path(temp)):
                    pass
            # All partial acquisitions were released after contention.
            with controller.locks("/dev/cu.usbmodem2", x4.MAC, Path(temp)):
                pass

    def test_existing_cam_namespace_contends(self):
        with tempfile.TemporaryDirectory() as temp, patch.object(controller.cam_device, "LOCK_DIR", Path(temp)):
            with controller.cam_device.DeviceLocks("/dev/cu.usbserial-1"):
                with self.assertRaises(BlockingIOError):
                    with controller.locks("/dev/tty.usbserial-1", controller.cam_device.MAC):
                        pass

    def test_wrong_identity_refuses_even_same_port(self):
        usb = {"port": "/dev/cu.usbmodem1", "vid": 0x303a, "pid": 0x1001,
               "serial_number": x4.MAC.upper()}
        x4.identity(usb, x4.CHIP, x4.MAC.upper(), x4.TARGET)
        for field, value in (("vid", 0x1a86), ("serial_number", controller.cam_device.MAC)):
            wrong = dict(usb, **{field: value})
            with self.assertRaises(ValueError):
                x4.identity(wrong, x4.CHIP, x4.MAC, x4.TARGET)
        for chip, mac, model in (("ESP32-C3", x4.MAC, x4.TARGET),
                                 (x4.CHIP, controller.cam_device.MAC, x4.TARGET),
                                 (x4.CHIP, x4.MAC, "t5s3-pro")):
            with self.assertRaises(ValueError):
                x4.identity(usb, chip, mac, model)
        with self.assertRaisesRegex(RuntimeError, "disabled"):
            x4.live()

    def test_exact_manifest_and_wrong_artifacts(self):
        gh = GitHub()
        with tempfile.TemporaryDirectory() as temp:
            self.assertEqual(x4.unpack(gh, gh.run, gh.artifact, SHA, Path(temp) / "ok"), DIGEST)
            cases = []
            for key, value in (("target", "cam"), ("source_sha", "b" * 40), ("run_attempt", 2)):
                info = x4.manifest(IMAGE, SHA, 7, 1)
                info[key] = value
                cases.append(archive(info))
            info = x4.manifest(IMAGE, SHA, 7, 1)
            info["firmware"]["offset"] = 0
            cases.extend([archive(info), archive(extra="../../escape"), archive(extra="private.jpg"),
                          archive(extra=x4.IMAGE)])
            for blob in cases:
                gh.blob = blob
                with self.assertRaises(ValueError):
                    x4.unpack(gh, gh.run, gh.artifact, SHA, Path(temp) / "bad")
            for image in (b"cam", IMAGE.replace(x4.MARKER, b"RISCRTE_BOARD_ID:t5s3-pro"),
                          IMAGE[:12] + b"\x05\x00" + IMAGE[14:]):
                with self.assertRaises(ValueError):
                    x4.image_digest(image)

    def test_provenance_owner_fork_run_target(self):
        gh = GitHub()
        self.assertEqual(controller.candidate(gh, 350, SHA, "x4")[0]["id"], 7)
        for field, value in (("actor", {"login": "stranger"}), ("workflow_id", 999),
                             ("head_sha", "b" * 40), ("head_repository", {"id": 99})):
            wrong = GitHub()
            wrong.run[field] = value
            self.assertIsNone(controller.candidate(wrong, 350, SHA, "x4"))
        gh.pr["user"] = {"login": "stranger"}
        with self.assertRaises(ValueError):
            controller.cam.eligible_source(gh, 350)
        gh = GitHub()
        gh.pr["head"]["repo"] = {"id": 99, "full_name": "fork/repo"}
        with self.assertRaises(ValueError):
            controller.cam.eligible_source(gh, 350)
        gh = GitHub()
        gh.artifact["name"] = "cam-candidate-" + SHA
        with self.assertRaises(ValueError):
            controller.candidate(gh, 350, SHA, "x4")

    def test_download_retry_status_retry_and_dedup(self):
        gh = GitHub()
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            gh.fail_download = True
            with self.assertRaises(OSError):
                controller.once(gh, 350, root, "x4", True)
            gh.fail_download = False
            gh.fail_status = True
            with self.assertRaises(OSError):
                controller.once(gh, 350, root, "x4", True)
            gh.fail_status = False
            result = controller.once(gh, 350, root, "x4", True)
            controller.once(gh, 350, root, "x4", True)
            self.assertEqual(gh.downloads, 2)
            self.assertEqual(len(gh.posts), 1)
            self.assertEqual(result["hardware"], "not_run")
            self.assertEqual(set(result["checks"].values()), {"not_run"})
            self.assertEqual(gh.posts[0][1]["context"], "X4 candidate / dry-run")
            self.assertIn("hardware not run", gh.posts[0][1]["description"])
            self.assertEqual(gh.posts[0][0], "/statuses/" + SHA)

    def test_two_targets_isolated_and_failure_does_not_starve_cam(self):
        gh = GitHub()
        with tempfile.TemporaryDirectory() as temp:
            with patch.object(controller, "once", side_effect=[OSError("disconnected"), {"target": "cam"}]) as call:
                results = controller.scan(gh, [350], Path(temp), ("x4", "cam"))
                self.assertEqual(call.call_count, 2)
                self.assertEqual(results[-1]["target"], "cam")
            with patch.object(controller.cam, "candidate", return_value=None):
                cam = controller.once(gh, 350, Path(temp), "cam")
                x = controller.once(gh, 350, Path(temp), "x4")
                self.assertEqual(cam["artifact"], "waiting")
                self.assertEqual(x["artifact"], "verified")

    def test_failure_and_incomplete_journal_never_pass(self):
        gh = GitHub()
        gh.blob = b"not zip"
        with tempfile.TemporaryDirectory() as temp:
            result = controller.once(gh, 350, Path(temp), "x4", True)
            self.assertEqual(result["artifact"], "rejected")
            self.assertEqual(gh.posts[0][1]["state"], "error")
        with tempfile.TemporaryDirectory() as temp:
            (Path(temp) / "dry-run/x4" / SHA).mkdir(parents=True)
            with self.assertRaises(RuntimeError):
                controller.once(GitHub(), 350, Path(temp), "x4")

    def test_rate_limit_and_head_movement_fail_closed(self):
        gh = GitHub()
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            with patch.object(controller, "candidate", side_effect=controller.cam.RateLimited("limited")):
                with self.assertRaises(controller.cam.RateLimited):
                    controller.once(gh, 350, root, "x4")
            self.assertFalse((root / "dry-run/x4" / SHA).exists())
            with patch.object(controller.cam, "eligible_source", side_effect=[SHA, "b" * 40]):
                result = controller.once(gh, 350, root, "x4", True)
            self.assertEqual(result["artifact"], "rejected")
            self.assertEqual(gh.posts[0][1]["state"], "error")

    def test_scheduler_contention_and_cli_requires_dry_run(self):
        import subprocess
        import sys
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            with (root / "candidate-scan.lock").open("a+") as lock:
                controller.fcntl.flock(lock.fileno(), controller.fcntl.LOCK_EX | controller.fcntl.LOCK_NB)
                with self.assertRaises(BlockingIOError):
                    controller.scan(GitHub(), [350], root)
            result = subprocess.run([sys.executable, controller.__file__, "--pr", "350",
                                     "--evidence-root", temp], capture_output=True, timeout=5)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn(b"--dry-run", result.stderr)

    def observe(self, lines, disconnect=False):
        class Stream:
            def __init__(self):
                self.data = ("\n".join(lines) + "\n").encode()
            def read(self, size):
                if disconnect and not self.data:
                    raise OSError("private bytes must not escape")
                part, self.data = self.data[:size], self.data[size:]
                return part
        ticks = iter(i / 10 for i in range(1000))
        return x4.observe(Stream(), SHA, DIGEST, clock=lambda: next(ticks), seconds=1)

    def test_bounded_serial_all_components_and_privacy(self):
        good = ["[100] [INF] [X4] diagnostic boot RISCRTE_BOARD_ID:xteink-x4-pro flash=16MB app0=0x10000",
                "[100] [INF] [X4] present complete elapsed=1800", "[100] [INF] [X4] storage.volume mounted=1 reason=ok",
                "[100] [INF] [X4] input.navigation event=left sequence=1"]
        self.assertEqual(self.observe(good)["result"], "pass")
        for i, key in enumerate(x4.TESTS):
            r = self.observe(good[:i] + good[i+1:])
            self.assertEqual(r["checks"][key], "missing")
            self.assertEqual(r["result"], "failed")
        for lines in (good + ["Guru Meditation Error"], good + [good[0]], ["A" * 513],
                      ["RUNTIME BOOT state=Running", "CAMERA_APP saved=/private.jpg bytes=123"]):
            self.assertEqual(self.observe(lines)["result"], "failed")
        self.assertEqual(self.observe(good, disconnect=True)["result"], "failed")
        r = self.observe(good + ["CAMERA_PRIVATE image-secret", "[100] [INF] [X4] arbitrary secret", "wifi_password=secret"])
        self.assertNotIn("secret", json.dumps(r))
        self.assertEqual(r["source_sha"], SHA)
        self.assertEqual(r["firmware_sha256"], DIGEST)


if __name__ == "__main__":
    unittest.main()
