import hashlib
import io
import json
import os
from pathlib import Path
import sys
import tempfile
import unittest
import zipfile

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent / "cam"))
sys.path.insert(0, str(HERE))
import x4_controller as controller
import x4_ci_device as device

SHA = "a" * 40
IMAGE = b"\xe9" + b"RISCRTE_BOARD_ID:xteink-x4-pro" + b"\x00" * 100


class FakeGitHub:
    def __init__(self, artifact_bytes=None, conclusion="success"):
        self.blob = artifact_bytes
        self.conclusion = conclusion
        self.posts = []

    def call(self, path, method="GET", body=None, limit=1_000_000):
        if path.startswith("/actions/workflows/"):
            run = {"event": "pull_request", "head_sha": SHA,
                   "actor": {"login": controller.OWNER}, "status": "completed",
                   "conclusion": self.conclusion, "id": 7, "run_attempt": 1,
                   "pull_requests": [{"number": 350, "head": {"sha": SHA, "repo": {"id": 42}},
                                      "base": {"repo": {"id": 42}}}]}
            return {"total_count": 1, "workflow_runs": [run]}
        if path == "/actions/runs/7/artifacts?per_page=100":
            return {"artifacts": [{"id": 8, "name": "x4-app-candidate-" + SHA,
                    "size_in_bytes": len(self.blob), "expired": False,
                    "digest": "sha256:" + hashlib.sha256(self.blob).hexdigest(),
                    "workflow_run": {"head_sha": SHA}}]}
        if path == "/actions/artifacts/8/zip":
            return self.blob
        if path.startswith("/statuses/"):
            self.posts.append((path, method, body))
            return {"id": len(self.posts)}
        raise AssertionError(path)


def archive(extra=False):
    manifest = {"schema": 1, "board": "xteink-x4-pro", "source_sha": SHA,
                "run_id": 7, "run_attempt": 1,
                "firmware": {"file": "firmware.bin", "offset": 0x10000,
                             "bytes": len(IMAGE), "sha256": hashlib.sha256(IMAGE).hexdigest()}}
    buf = io.BytesIO()
    with zipfile.ZipFile(buf, "w") as out:
        out.writestr("firmware.bin", IMAGE)
        out.writestr("manifest.json", json.dumps(manifest))
        if extra:
            out.writestr("../../run-me.py", "print('bad')")
    return buf.getvalue()


class X4ControllerTests(unittest.TestCase):
    def test_exact_head_artifact_is_data_only(self):
        gh = FakeGitHub(archive())
        run, artifact = controller.candidate(gh, 350, SHA)
        with tempfile.TemporaryDirectory() as temp:
            output = Path(temp) / "candidate"
            got = controller.unpack_candidate(gh, run, artifact, SHA, output)
            self.assertEqual(got, hashlib.sha256(IMAGE).hexdigest())
            self.assertEqual({p.name for p in output.iterdir()}, {"firmware.bin", "manifest.json"})
        bad = FakeGitHub(archive(extra=True))
        run, artifact = controller.candidate(bad, 350, SHA)
        with tempfile.TemporaryDirectory() as temp:
            with self.assertRaises(ValueError):
                controller.unpack_candidate(bad, run, artifact, SHA, Path(temp) / "candidate")

    def test_cloud_failure_cannot_be_success(self):
        with self.assertRaises(controller.cam.CloudBuildFailed):
            controller.candidate(FakeGitHub(archive(), conclusion="failure"), 350, SHA)

    def test_status_requires_boot_and_preserved_regions(self):
        result = {"source_sha": SHA, "firmware_sha256": "b" * 64,
                  "result": "pass", "device_exit": 0,
                  "device": {"result": "pass", "candidate_readback_equal": True,
                             "protected_equal": True, "visual_qualified": False,
                             "boot": {"home_present": True, "boot_splash_present": True,
                                      "ready_heartbeats": 3, "home_geometry": "480x800"}}}
        self.assertEqual(controller.terminal_state(result), "success")
        for key in ("candidate_readback_equal", "protected_equal"):
            changed = json.loads(json.dumps(result))
            changed["device"][key] = False
            self.assertEqual(controller.terminal_state(changed), "error")
        changed = json.loads(json.dumps(result))
        changed["device"]["boot"]["ready_heartbeats"] = 2
        self.assertEqual(controller.terminal_state(changed), "error")

    def test_app0_bounds_and_boot_markers(self):
        self.assertLessEqual(device.validate_image(IMAGE, hashlib.sha256(IMAGE).hexdigest()),
                             device.APP_END)
        with self.assertRaisesRegex(RuntimeError, "incompatible"):
            device.validate_image(IMAGE + b"\0" * (0x640000 + 1),
                                  hashlib.sha256(IMAGE + b"\0" * (0x640000 + 1)).hexdigest())
        lines = ["[X4] diagnostic entered",
                 "[X4] diagnostic boot RISCRTE_BOARD_ID:xteink-x4-pro",
                 "[X4] storage.volume mounted=1 reason=",
                 "[X4] boot splash present=1",
                 "[X4] home geometry width=480 height=800 menu_height=310",
                 "[X4] home activity scheduled=1", "[X4] home present=1"]
        lines += ["[X4] heartbeat ready=1"] * 3
        self.assertFalse(device.validate_boot(lines)["visual_qualified"])
        with self.assertRaisesRegex(RuntimeError, "steady heartbeat"):
            device.validate_boot(lines[:-2])
        with self.assertRaisesRegex(RuntimeError, "exactly once"):
            device.validate_boot(lines + ["[X4] diagnostic entered"])

    def test_private_binding_and_no_duplicate_success(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            binding = root / "binding.json"
            binding.write_text(json.dumps({"mac": device.MAC, "location": "2-4"}))
            os.chmod(binding, 0o600)
            self.assertEqual(controller.load_binding(binding)["location"], "2-4")
            os.chmod(binding, 0o644)
            with self.assertRaises(ValueError):
                controller.load_binding(binding)
            gh = FakeGitHub(archive())
            controller.post_status(gh, SHA, "pending", "X4 running")
            self.assertEqual(gh.posts[0][0], "/statuses/" + SHA)
            self.assertEqual(gh.posts[0][2]["context"], controller.STATUS_CONTEXT)


if __name__ == "__main__":
    unittest.main()
