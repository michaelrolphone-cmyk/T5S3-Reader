#!/usr/bin/env python3
"""Source-level contract for automatic release triggering."""

from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
WORKFLOW = (ROOT / ".github/workflows/release.yml").read_text(encoding="utf-8")

required = (
    "name: Cut RiscRTE release",
    "push:",
    "branches:",
    "- master",
    '"platformio.ini"',
    '"Apps/**/*.json"',
    '"Drivers/**/manifest.json"',
    "workflow_dispatch:",
    "python scripts/publish_updated_packages.py --plan-only",
)
for token in required:
    assert token in WORKFLOW, f"release workflow is missing automatic trigger contract: {token}"

print("Cut release workflow: master version-file pushes and manual dispatch enabled PASS")
