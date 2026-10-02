import hashlib
import io
import json
import tempfile
import unittest
import os
import ssl
import struct
import zipfile
from pathlib import Path

import trusted_controller as controller
import ci_device as device


SHA = "a" * 40


class FakeGitHub:
    def __init__(self, owner=True, same_repo=True):
        self.owner = owner
        self.same_repo = same_repo

    def call(self, path, method="GET", body=None, limit=1_000_000):
        if path.startswith("/pulls/"):
            repo = {"full_name": controller.REPO, "id": 42}
            head_repo = repo if self.same_repo else {"full_name": "elsewhere/fork", "id": 99}
            return {"state": "open", "head": {"sha": SHA, "repo": head_repo},
                    "base": {"repo": repo},
                    "user": {"login": controller.OWNER if self.owner else "stranger"}}
        raise AssertionError(path)


class ControllerTests(unittest.TestCase):
    def test_qualified_cam_has_two_ota_slots(self):
        table = bytearray(b"\xff" * 4096)
        entries = [(1, 2, 0x9000, 0x5000, b"nvs"),
                   (1, 0, 0xE000, 0x2000, b"otadata"),
                   (0, 16, 0x10000, 0x300000, b"app0"),
                   (0, 17, 0x310000, 0x300000, b"app1")]
        for index, (kind, subtype, offset, size, label) in enumerate(entries):
            table[index * 32:(index + 1) * 32] = struct.pack(
                "<HBBII16sI", 0x50AA, kind, subtype, offset, size, label, 0)
        self.assertEqual(device.partition(bytes(table), 541056), 0x10000)
        table[3 * 32:4 * 32] = b"\xff" * 32
        with self.assertRaises(RuntimeError):
            device.partition(bytes(table), 541056)

    def test_https_uses_verified_system_ca_context(self):
        gh = controller.GitHub("test-only-not-a-credential")
        self.assertTrue(gh.ssl_context.check_hostname)
        self.assertEqual(gh.ssl_context.verify_mode, ssl.CERT_REQUIRED)
        self.assertGreater(gh.ssl_context.cert_store_stats()["x509_ca"], 0)

    def test_private_cam_binding(self):
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / "cam.json"
            path.write_text(json.dumps({"port": "/dev/cu.usbserial-2310",
                "location": "2-3.1", "mac": "28:84:85:4b:57:98"}))
            os.chmod(path, 0o600)
            self.assertEqual(controller.load_cam_binding(path)["location"], "2-3.1")
            os.chmod(path, 0o644)
            with self.assertRaises(ValueError):
                controller.load_cam_binding(path)

    def test_automatic_owner_source_gate(self):
        self.assertEqual(controller.eligible_source(FakeGitHub(), 123), SHA)
        with self.assertRaises(ValueError):
            controller.eligible_source(FakeGitHub(owner=False), 123)
        with self.assertRaises(ValueError):
            controller.eligible_source(FakeGitHub(same_repo=False), 123)

    def test_exact_sha_commit_status_and_states(self):
        class StatusGH:
            def __init__(self):
                self.posts = []

            def call(self, path, method="GET", body=None, limit=1_000_000):
                self.posts.append((path, method, body))
                return {"id": len(self.posts)}

        gh = StatusGH()
        for state in ("pending", "success", "failure", "error"):
            controller.post_status(gh, SHA, state, "numeric result; no image")
        self.assertEqual([post[0] for post in gh.posts], ["/statuses/" + SHA] * 4)
        self.assertEqual([post[2]["state"] for post in gh.posts],
                         ["pending", "success", "failure", "error"])
        self.assertEqual({post[2]["context"] for post in gh.posts},
                         {controller.STATUS_CONTEXT})
        with self.assertRaises(ValueError):
            controller.post_status(gh, "not-a-sha", "success", "bad")

    def test_retry_never_turns_unrestored_result_into_success(self):
        class StatusGH(FakeGitHub):
            def __init__(self):
                super().__init__()
                self.posts = []

            def call(self, path, method="GET", body=None, limit=1_000_000):
                if path.startswith("/statuses/"):
                    self.posts.append((path, body))
                    return {"id": 7}
                return super().call(path, method, body, limit)

        gh = StatusGH()
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            folder = root / SHA
            folder.mkdir()
            result = {"schema": 1, "source_sha": SHA, "result": "pass", "device_exit": 0,
                      "status_state": "success", "firmware_sha256": "b" * 64,
                      "device": {"result": "pass", "candidate_readback_equal": True,
                                 "baseline_restored": False}}
            saved = folder / "result.json"
            saved.write_text(json.dumps(result))
            with self.assertRaises(RuntimeError):
                controller.once(gh, 123, root, Path("/unused"), {})
            self.assertEqual(gh.posts, [])
            result["source_sha"] = "c" * 40
            saved.write_text(json.dumps(result))
            with self.assertRaises(RuntimeError):
                controller.once(gh, 123, root, Path("/unused"), {})
            self.assertEqual(gh.posts, [])
            result["source_sha"] = SHA
            result["device"]["baseline_restored"] = True
            result["device"]["image_bytes"] = 24069
            saved.write_text(json.dumps(result))
            controller.once(gh, 123, root, Path("/unused"), {})
            self.assertEqual(gh.posts[0][0], "/statuses/" + SHA)
            self.assertEqual(gh.posts[0][1]["state"], "success")
            self.assertIn("capture 24069 B", gh.posts[0][1]["description"])
            self.assertEqual(json.loads(saved.read_text())["status_id"], 7)

    def test_status_fails_closed_on_missing_or_skipped_device_proof(self):
        base = {"source_sha": SHA, "firmware_sha256": "b" * 64,
                "result": "pass", "device_exit": 0,
                "device": {"result": "pass", "candidate_readback_equal": True,
                           "baseline_restored": True}}
        self.assertEqual(controller.terminal_state(base), "success")
        for field in ("baseline_restored", "candidate_readback_equal"):
            missing = json.loads(json.dumps(base))
            missing["device"][field] = False
            self.assertEqual(controller.terminal_state(missing), "error")
        skipped = json.loads(json.dumps(base))
        skipped["device"]["result"] = "skipped"
        skipped["result"] = "failed"
        self.assertEqual(controller.terminal_state(skipped), "failure")
        failed = json.loads(json.dumps(base))
        failed["device_exit"] = 1
        failed["result"] = "failed"
        self.assertEqual(controller.terminal_state(failed), "failure")

    def test_artifact_hash_and_entry_boundary(self):
        image = b"candidate-image"
        manifest = {"schema": 1, "source_sha": SHA, "run_id": 7, "run_attempt": 1,
                    "firmware": {"file": "firmware.bin", "bytes": len(image),
                                 "sha256": hashlib.sha256(image).hexdigest()}}

        def archive(extra=False):
            data = io.BytesIO()
            with zipfile.ZipFile(data, "w") as z:
                z.writestr("firmware.bin", image)
                z.writestr("manifest.json", json.dumps(manifest))
                if extra:
                    z.writestr("../../escape", "bad")
            return data.getvalue()

        class ArtifactGH:
            def __init__(self, blob):
                self.blob = blob

            def call(self, *_args, **_kwargs):
                return self.blob

        run = {"id": 7, "run_attempt": 1}
        artifact = {"id": 8}
        with tempfile.TemporaryDirectory() as temp:
            digest = controller.unpack_candidate(ArtifactGH(archive()), run, artifact,
                                                 SHA, Path(temp) / "accepted")
            self.assertEqual(digest, hashlib.sha256(image).hexdigest())
            with self.assertRaises(ValueError):
                controller.unpack_candidate(ArtifactGH(archive(True)), run, artifact,
                                            SHA, Path(temp) / "rejected")


if __name__ == "__main__":
    unittest.main()
