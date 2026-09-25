import hashlib
import subprocess
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

from scripts.publish_updated_packages import publish_or_verify


class PublishRetryTest(unittest.TestCase):
    def test_retries_transient_release_creation_failure(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            payload = b"app payload"
            asset = root / "clock.elf"
            asset.write_bytes(payload)
            calls = []

            def fake_run(command, **kwargs):
                calls.append(command)
                if command[:4] == ["gh", "release", "view", "app-clock-v1.0.0"]:
                    return subprocess.CompletedProcess(
                        command, 1, "", "HTTP 404: release not found")
                if command[:4] == ["gh", "release", "create", "app-clock-v1.0.0"]:
                    if sum(call[:3] == ["gh", "release", "create"] for call in calls) == 1:
                        return subprocess.CompletedProcess(command, 1, "", "HTTP 500")
                    return subprocess.CompletedProcess(command, 0, "", "")
                raise AssertionError(f"unexpected gh command: {command}")

            candidate = {"product": "apps", "id": "clock", "version": "1.0.0"}
            record = {
                "tag": "app-clock-v1.0.0",
                "asset": "clock.elf",
                "sha256": hashlib.sha256(payload).hexdigest(),
            }
            with patch("scripts.publish_updated_packages.subprocess.run", side_effect=fake_run), \
                 patch("scripts.publish_updated_packages.time.sleep") as sleep:
                publish_or_verify(root, "source-sha", candidate, record, [asset])

            creates = [call for call in calls if call[:3] == ["gh", "release", "create"]]
            self.assertEqual(len(creates), 2)
            sleep.assert_called_once_with(1)


if __name__ == "__main__":
    unittest.main()
