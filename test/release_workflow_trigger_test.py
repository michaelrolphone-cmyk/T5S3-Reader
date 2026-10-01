#!/usr/bin/env python3
"""Source-level contract for automatic release triggering."""

from pathlib import Path
import re

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
    '"scripts/publish_updated_packages.py"',
    '"scripts/build_release_candidates.py"',
    "workflow_dispatch:",
    "python scripts/publish_updated_packages.py --plan-only",
)
for token in required:
    assert token in WORKFLOW, f"release workflow is missing automatic trigger contract: {token}"

print("Cut release workflow: version/package-planner pushes and manual dispatch enabled PASS")

# Validate the actual independent-job graph, not merely the trigger strings.
# A previous backmerge left these names present but spliced obsolete aggregate
# steps into them, including undefined steps.ver and a missing publish job.
job_matches = list(re.finditer(r"^  ([a-z_]+):\n", WORKFLOW, re.MULTILINE))
jobs = {match.group(1): WORKFLOW[match.end():
        job_matches[i + 1].start() if i + 1 < len(job_matches) else len(WORKFLOW)]
        for i, match in enumerate(job_matches) if match.start() > WORKFLOW.index("jobs:")}
assert set(jobs) == {"plan", "build_firmware", "build_apps", "build_drivers", "publish"}
for name, job in jobs.items():
    ids = set(re.findall(r"^        id: ([a-zA-Z0-9_-]+)$", job, re.MULTILINE))
    references = set(re.findall(r"steps\.([a-zA-Z0-9_-]+)\.", job))
    assert references <= ids, (name, "undefined step outputs", references - ids)
for product in ("firmware", "apps", "drivers"):
    job = jobs["build_" + product]
    assert f"needs.plan.outputs.{product} == 'true'" in job
    assert 'name: rte-release-plan' in job
    assert f'scripts/build_release_candidates.py --plan "$RUNNER_TEMP/release-plan/release-plan.json" --product {product}' in job
    assert f'scripts/verify_release_plan.py --plan "$RUNNER_TEMP/release-plan/release-plan.json" --product {product}' in job
    assert job.index('scripts/verify_release_plan.py') < job.index('uses: actions/upload-artifact@v4')
    assert f"needs.build_{product}.result == 'success'" in jobs['publish']
assert 'needs: [plan, build_firmware, build_apps, build_drivers]' in jobs['publish']
assert jobs['publish'].index('scripts/verify_release_plan.py') < jobs['publish'].index('scripts/publish_updated_packages.py')
assert 'cp -a dist/release-packages' in jobs['build_drivers']
assert 'cp -a dist/packages ' not in jobs['build_drivers']
assert 'git tag ' not in WORKFLOW and 'gh release create ' not in WORKFLOW
assert 'permissions:\n  contents: write' in WORKFLOW  # Existing token scope, no broader access.
print('Independent release job graph, artifact handoff, offline gates and failure gating PASS')
