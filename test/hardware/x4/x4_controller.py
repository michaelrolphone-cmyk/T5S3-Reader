#!/usr/bin/env python3
"""Pinned Mac X4 controller. PR artifacts are data, never Mac-executable code."""
import argparse
import hashlib
import io
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import zipfile

import trusted_controller as cam  # Separately pinned, reviewed GitHub client/source gate.

REPO = cam.REPO
OWNER = cam.OWNER
WORKFLOW = "x4-hardware-build.yml"
STATUS_CONTEXT = "X4 hardware / trusted owner SHA"
SHA = cam.SHA
MAX_ZIP = 10_000_000
MAX_IMAGE = 0x640000


def require(ok, message):
    if not ok:
        raise ValueError(message)


def load_binding(path):
    stat = path.lstat()
    require(path.is_file() and not path.is_symlink() and not stat.st_mode & 0o077
            and stat.st_size <= 512, "X4 binding is not a private regular file")
    value = json.loads(path.read_text())
    require(set(value) == {"mac", "location"} and value["mac"] == "84:c7:bb:79:e2:ac"
            and isinstance(value["location"], str)
            and re.fullmatch(r"[0-9]+-[0-9]+(?:\.[0-9]+)*", value["location"]),
            "X4 binding identity invalid")
    return value


def candidate(gh, number, sha):
    runs = gh.call(f"/actions/workflows/{WORKFLOW}/runs?event=pull_request&head_sha={sha}&per_page=100")
    require(runs["total_count"] <= 100, "X4 exact-head run history exceeds bound")
    related = []
    for run in runs["workflow_runs"]:
        prs = run.get("pull_requests") or []
        if (run["event"] == "pull_request" and run["head_sha"] == sha
                and run["actor"]["login"] == OWNER
                and any(p["number"] == number and p["head"]["sha"] == sha
                        and p["head"]["repo"]["id"] == p["base"]["repo"]["id"]
                        for p in prs)):
            related.append(run)
    if not related or any(r["status"] != "completed" for r in related):
        return None
    require(len(related) == 1, "X4 exact-head build ambiguous")
    run = related[0]
    if run["conclusion"] != "success":
        raise cam.CloudBuildFailed("X4 exact-head cloud build failed")
    listed = gh.call(f"/actions/runs/{run['id']}/artifacts?per_page=100")
    require(len(listed["artifacts"]) < 100, "X4 artifact page bound exceeded")
    matching = [a for a in listed["artifacts"]
                if a["name"] == "x4-app-candidate-" + sha and not a["expired"]
                and 0 < a["size_in_bytes"] <= MAX_ZIP
                and a.get("workflow_run", {}).get("head_sha") == sha]
    require(len(matching) == 1, "Expected one bounded exact-head X4 app artifact")
    return run, matching[0]


def unpack_candidate(gh, run, artifact, sha, folder):
    raw = gh.call(f"/actions/artifacts/{artifact['id']}/zip", limit=MAX_ZIP)
    require(len(raw) == artifact["size_in_bytes"], "X4 artifact ZIP byte count mismatch")
    expected_zip_sha = artifact.get("digest", "")
    if expected_zip_sha:
        require(expected_zip_sha == "sha256:" + hashlib.sha256(raw).hexdigest(),
                "X4 artifact ZIP digest mismatch")
    with zipfile.ZipFile(io.BytesIO(raw)) as archive:
        entries = archive.infolist()
        require(len(entries) == 2 and {e.filename for e in entries} == {"firmware.bin", "manifest.json"},
                "Unexpected X4 artifact entries")
        require(all(e.file_size <= MAX_IMAGE if e.filename == "firmware.bin" else e.file_size <= 2048
                    for e in entries), "X4 artifact entry exceeds bound")
        manifest = json.loads(archive.read("manifest.json"))
        image = archive.read("firmware.bin")
    info = manifest["firmware"]
    require(manifest["schema"] == 1 and manifest["board"] == "xteink-x4-pro"
            and manifest["source_sha"] == sha and manifest["run_id"] == run["id"]
            and manifest["run_attempt"] == run["run_attempt"]
            and info["file"] == "firmware.bin" and info["offset"] == 0x10000
            and info["bytes"] == len(image) and 0 < len(image) <= MAX_IMAGE
            and image[0] == 0xe9 and b"RISCRTE_BOARD_ID:xteink-x4-pro" in image
            and hashlib.sha256(image).hexdigest() == info["sha256"],
            "X4 image provenance, layout or hash mismatch")
    folder.mkdir(mode=0o700)
    (folder / "firmware.bin").write_bytes(image)
    (folder / "manifest.json").write_text(json.dumps(manifest, sort_keys=True) + "\n")
    return info["sha256"]


def post_status(gh, sha, state, description):
    require(state in {"pending", "success", "failure", "error"} and SHA.fullmatch(sha),
            "Invalid X4 status")
    return gh.call(f"/statuses/{sha}", "POST", {"state": state,
        "context": STATUS_CONTEXT, "description": description[:140]})


def safe_success(result):
    device = result.get("device", {})
    boot = device.get("boot", {})
    return (result.get("result") == "pass" and result.get("device_exit") == 0
            and device.get("result") == "pass" and device.get("candidate_readback_equal") is True
            and device.get("protected_equal") is True and device.get("visual_qualified") is False
            and boot.get("home_present") is True and boot.get("boot_splash_present") is True
            and boot.get("ready_heartbeats", 0) >= 3 and boot.get("home_geometry") == "480x800"
            and SHA.fullmatch(result.get("source_sha", "")) is not None
            and re.fullmatch(r"[0-9a-f]{64}", result.get("firmware_sha256", "")) is not None)


def terminal_state(result):
    if safe_success(result):
        return "success"
    device = result.get("device", {})
    if device.get("prewrite_restored") is True and device.get("restored_boot", {}).get("home_present") is True:
        return "failure"
    return "error"


def describe(result):
    state = result["status_state"]
    if state == "success":
        return "X4 boot pass; app readback/protected bytes match; visual/buttons untested"
    return ("X4 " + state + "; inspect private bounded hardware journal")[:140]


def save(path, result):
    temp = path.with_suffix(".tmp")
    temp.write_text(json.dumps(result, indent=2) + "\n")
    temp.replace(path)


def once(gh, number, evidence_root, python, binding, pause_file):
    sha = cam.eligible_source(gh, number)
    if pause_file.exists():
        return
    folder = evidence_root / sha
    journal = folder / "result.json"
    if folder.exists():
        if journal.exists():
            old = json.loads(journal.read_text())
            require(old.get("source_sha") == sha, "X4 journal SHA mismatch")
            if old.get("status_id"):
                return
            if old.get("status_state") in {"success", "failure", "error"}:
                require(old["status_state"] != "success" or safe_success(old),
                        "X4 success journal lacks complete proof")
                old["status_id"] = post_status(gh, sha, old["status_state"], describe(old))["id"]
                save(journal, old)
                return
            old["status_state"] = "error"  # Interrupted transaction is never retried.
            old["error"] = "Incomplete prior X4 hardware transaction; inspect private journal"
            save(journal, old)
            old["status_id"] = post_status(gh, sha, "error", describe(old))["id"]
            save(journal, old)
            return
        error = {"schema": 1, "pr": number, "source_sha": sha,
                 "status_state": "error", "error": "Incomplete private X4 transaction"}
        save(journal, error)
        error["status_id"] = post_status(gh, sha, "error", describe(error))["id"]
        save(journal, error)
        return
    try:
        found = candidate(gh, number, sha)
    except cam.RateLimited:
        raise
    except Exception as exc:
        folder.mkdir(mode=0o700, parents=True)
        result = {"schema": 1, "pr": number, "source_sha": sha,
                  "status_state": "failure" if isinstance(exc, cam.CloudBuildFailed) else "error",
                  "error": f"{type(exc).__name__}: {exc}"[:300]}
        save(journal, result)
        result["status_id"] = post_status(gh, sha, result["status_state"], describe(result))["id"]
        save(journal, result)
        return
    if found is None:
        return
    folder.mkdir(mode=0o700, parents=True)
    result = {"schema": 1, "pr": number, "source_sha": sha, "result": "failed"}
    try:
        run, artifact = found
        result["run_id"] = run["id"]
        digest = unpack_candidate(gh, run, artifact, sha, folder / "candidate")
        result["firmware_sha256"] = digest
        result["pending_status_id"] = post_status(gh, sha, "pending",
            "X4 app-only hardware boot check running; visual/buttons untested")["id"]
        save(journal, result)
        device_dir = folder / "device"
        command = [str(python), str(Path(__file__).with_name("x4_ci_device.py")),
                   "--image", str(folder / "candidate/firmware.bin"),
                   "--sha256", digest, "--out", str(device_dir),
                   "--location", binding["location"]]
        env = os.environ.copy()
        env.pop("GH_TOKEN", None)
        completed = subprocess.run(command, capture_output=True, text=True,
                                   env=env, timeout=1600)
        if completed.returncode == 75 and not device_dir.exists():
            # Shared lock was held before any device access; retain pending
            # status and retry safely on the next natural timer tick.
            shutil.rmtree(folder)
            return
        result["device_exit"] = completed.returncode
        if (device_dir / "result.json").exists():
            result["device"] = json.loads((device_dir / "result.json").read_text())
        result["result"] = "pass" if completed.returncode == 0 else "failed"
        result["status_state"] = terminal_state(result)
    except Exception as exc:
        result["error"] = f"{type(exc).__name__}: {exc}"[:300]
        result["status_state"] = terminal_state(result)
    finally:
        if folder.exists():
            save(journal, result)
            result["status_id"] = post_status(gh, sha, result["status_state"], describe(result))["id"]
            save(journal, result)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--evidence-root", type=Path, required=True)
    parser.add_argument("--binding", type=Path, required=True)
    parser.add_argument("--pause-file", type=Path, required=True)
    parser.add_argument("--python", type=Path, default=Path(sys.executable))
    args = parser.parse_args()
    token = os.environ.get("GH_TOKEN")
    if token is None:
        found = subprocess.run(["/usr/bin/security", "find-generic-password", "-w",
                                "-s", "riscrte-cam-ci", "-a", OWNER],
                               capture_output=True, text=True, timeout=10)
        if found.returncode == 0:
            token = found.stdout.strip()
    gh = cam.GitHub(token)
    binding = load_binding(args.binding)
    prs = gh.call("/pulls?state=open&per_page=100")
    require(len(prs) < 100, "Open PR page bound exceeded")
    for pr in cam.bounded_owner_prs(prs):
        try:
            once(gh, pr["number"], args.evidence_root, args.python, binding, args.pause_file)
        except ValueError as exc:
            print(f"PR {pr['number']}: {exc}", file=sys.stderr)


if __name__ == "__main__":
    main()
