import hashlib
import io
import json
import tempfile
import unittest
import os
import zipfile
from pathlib import Path

import trusted_controller as controller


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
