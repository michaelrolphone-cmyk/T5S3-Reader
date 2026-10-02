#!/usr/bin/env python3
"""Pinned multi-target controller. X4 live execution remains disabled.

This entry point validates cloud artifacts only. The existing CAM controller
and its live timer remain unchanged. Install separately only after review.
"""
import argparse
import fcntl
import json
import os
import re
from pathlib import Path
import sys
import time
import zipfile

sys.path.insert(0, str(Path(__file__).resolve().parent / "cam"))
import trusted_controller as cam
import ci_device as cam_device
from x4 import contract as x4

TARGETS = ("cam", "x4")


from device_locks import locks


def candidate(gh, number, sha, target):
    if target == "cam":
        return cam.candidate(gh, number, sha)
    x4.require(target == "x4", "Unknown hardware target")
    repo = gh.call("")
    workflow = gh.call(f"/actions/workflows/{x4.WORKFLOW}")
    x4.require(workflow["path"] == ".github/workflows/" + x4.WORKFLOW,
               "X4 workflow identity mismatch")
    runs = gh.call(f"/actions/workflows/{x4.WORKFLOW}/runs?event=pull_request&head_sha={sha}&per_page=100")
    x4.require(runs["total_count"] <= 100, "X4 run history exceeds bound")
    related = [r for r in runs["workflow_runs"]
               if r["event"] == "pull_request" and r["head_sha"] == sha
               and r["workflow_id"] == workflow["id"]
               and r["repository"]["id"] == repo["id"]
               and r["head_repository"]["id"] == repo["id"]
               and r["actor"]["login"] == cam.OWNER
               and r.get("triggering_actor", r["actor"])["login"] == cam.OWNER
               and any(p["number"] == number and p["head"]["sha"] == sha
                       and p["head"]["repo"]["id"] == repo["id"]
                       and p["base"]["repo"]["id"] == repo["id"]
                       for p in (r.get("pull_requests") or []))]
    if not related:
        return None
    x4.require(len(related) == 1, "X4 exact-head build ambiguous")
    run = related[0]
    if run["status"] != "completed":
        return None
    if run["conclusion"] != "success":
        raise cam.CloudBuildFailed("X4 cloud build failed")
    artifacts = gh.call(f"/actions/runs/{run['id']}/artifacts?per_page=100")
    x4.require(artifacts["total_count"] <= 100, "X4 artifact count exceeds bound")
    matching = [a for a in artifacts["artifacts"] if a["name"] == x4.PREFIX + sha
                and not a["expired"] and 0 < a["size_in_bytes"] <= x4.MAX_ZIP]
    # No fallback to EPD47, CAM, or a previous commit. Master may not yet
    # contain the X4 environment from the separately owned firmware PR.
    x4.require(len(matching) == 1, "X4 candidate unavailable or ambiguous")
    return run, matching[0]


def save(path, result):
    temp = path.with_suffix(".tmp")
    temp.write_text(json.dumps(result, sort_keys=True, indent=2) + "\n")
    os.chmod(temp, 0o600)
    temp.replace(path)


def report(gh, result, path):
    if result.get("status_id"):
        return
    # This scanner can NEVER post a hardware success. A separately named
    # artifact-readiness status says only what was actually validated.
    state = "success" if result["artifact"] == "verified" else "error"
    status = gh.call(f"/statuses/{result['source_sha']}", "POST", {
        "context": result["target"].upper() + " candidate / dry-run",
        "state": state, "description": "Artifact " + result["artifact"] + "; hardware not run"})
    result["status_id"] = status["id"]
    save(path, result)


def once(gh, number, root, target, publish=False):
    x4.require(target in TARGETS, "Unknown hardware target")
    sha = cam.eligible_source(gh, number)
    # Separate from the existing CAM physical journal and from each other.
    folder = root / "dry-run" / target / sha
    path = folder / "result.json"
    if path.exists():
        x4.require(path.stat().st_size <= 4096, "Oversized dry-run journal")
        result = json.loads(path.read_text())
        x4.require(result.get("source_sha") == sha and result.get("target") == target
                   and result.get("mode") == "dry-run" and result.get("hardware") == "not_run"
                   and result.get("artifact") in ("verified", "rejected"), "Journal identity invalid")
        if publish:
            report(gh, result, path)
        return result
    if folder.exists():
        raise RuntimeError("Incomplete dry-run journal; inspect before retry")
    # Transient network/disconnect/rate-limit errors before acceptance retry
    # next scan; they never become a completed device result.
    try:
        found = candidate(gh, number, sha, target)
    except (ValueError, KeyError):
        found = False
    if found is None:
        return {"target": target, "source_sha": sha, "artifact": "waiting", "hardware": "not_run"}
    result = {"schema": 1, "mode": "dry-run", "target": target, "source_sha": sha,
              "pr": number, "artifact": "rejected", "hardware": "not_run",
              "checks": {key: "not_run" for key in x4.TESTS} if target == "x4" else {}}
    folder.mkdir(mode=0o700, parents=True)
    if found:
        run, artifact = found
        try:
            digest = (x4.unpack if target == "x4" else cam.unpack_candidate)(
                gh, run, artifact, sha, folder / "candidate")
            # A moved/closed head cannot inherit this candidate's readiness.
            x4.require(cam.eligible_source(gh, number) == sha, "PR head moved during validation")
            result.update(artifact="verified", firmware_sha256=digest,
                          run_id=run["id"], run_attempt=run["run_attempt"], artifact_id=artifact["id"])
        except (cam.RateLimited, OSError):
            # No physical work occurred; remove only our empty journal dir so
            # artifact download can be retried. Never touch a live CAM journal.
            if not (folder / "candidate").exists():
                folder.rmdir()
            raise
        except (ValueError, KeyError, RuntimeError, zipfile.BadZipFile):
            pass
    save(path, result)
    if publish:
        report(gh, result, path)
    return result


def scan(gh, numbers, root, targets=TARGETS, publish=False):
    x4.require(0 < len(numbers) <= cam.MAX_OWNER_PRS and len(set(targets)) == len(targets)
               and set(targets) <= set(TARGETS) and targets, "Invalid bounded scan")
    root.mkdir(mode=0o700, parents=True, exist_ok=True)
    results = []
    # One controller scan at a time per evidence root. Device locks above use
    # the global lab namespace; no device lock/open is needed for a dry run.
    with (root / "candidate-scan.lock").open("a+") as lock:
        fcntl.flock(lock.fileno(), fcntl.LOCK_EX | fcntl.LOCK_NB)
        deadline = time.monotonic() + 180
        for number in numbers:
            for target in targets:
                if time.monotonic() >= deadline:
                    return results
                try:
                    results.append(once(gh, number, root, target, publish))
                except cam.RateLimited:
                    return results
                except (OSError, RuntimeError, ValueError, KeyError):
                    results.append({"target": target, "artifact": "retry_or_inspect", "hardware": "not_run"})
    return results


def hardware_success(result):
    device = result.get("device", {})
    return (result.get("mode") == "hardware" and result.get("target") == "x4"
            and cam.SHA.fullmatch(result.get("source_sha", "")) is not None
            and device.get("source_sha") == result["source_sha"]
            and device.get("firmware_sha256") == result.get("firmware_sha256")
            and isinstance(result.get("firmware_sha256"), str)
            and re.fullmatch(r"[0-9a-f]{64}", result["firmware_sha256"]) is not None
            and device.get("target") == x4.TARGET and device.get("mac") == x4.MAC
            and device.get("mode") == "hardware" and device.get("result") == "pass"
            and all(device.get(key) is True for key in ("identity_verified", "candidate_readback_equal",
                                                       "metadata_preserved", "closed"))
            and device.get("manual_recovery_required") is False
            and device.get("checks") == {key: "pass" for key in x4.TESTS})


def hardware_report(gh, result, path):
    if result.get("status_id"):
        return
    x4.require(result.get("status_state") in ("success", "failure", "error"), "Incomplete hardware journal")
    x4.require(result["status_state"] != "success" or hardware_success(result),
               "X4 success lacks exact target/device evidence")
    status = gh.call(f"/statuses/{result['source_sha']}", "POST", {
        "context": "X4 hardware / trusted owner SHA", "state": result["status_state"],
        "description": "X4 " + result["status_state"] + "; exact candidate; serial diagnostics only"})
    result["status_id"] = status["id"]
    save(path, result)


def _hardware_once(gh, number, root, profile, runner):
    """Pinned transaction orchestration, tested with fake runner/API only.

    Public entry and the real adapter are independently gated. No CLI switch
    reaches this implementation while execution approval remains unresolved.
    """
    sha = cam.eligible_source(gh, number)
    folder = root / "hardware" / "x4" / sha
    path = folder / "result.json"
    if path.exists():
        x4.require(path.stat().st_size <= 8192, "X4 journal oversized")
        prior = json.loads(path.read_text())
        x4.require(prior.get("source_sha") == sha and prior.get("target") == "x4"
                   and prior.get("mode") == "hardware", "X4 journal identity mismatch")
        hardware_report(gh, prior, path)
        return prior
    if folder.exists():
        raise RuntimeError("Incomplete X4 hardware journal; manual recovery required; no retry")
    try:
        found = candidate(gh, number, sha, "x4")
    except (ValueError, KeyError):
        found = False
    if found is None:
        return None
    folder.mkdir(mode=0o700, parents=True)
    result = {"mode": "hardware", "target": "x4", "source_sha": sha, "pr": number,
              "status_state": "error"}
    if not found:
        save(path, result)
        hardware_report(gh, result, path)
        return result
    run, artifact = found
    result.update(run_id=run["id"], run_attempt=run["run_attempt"], artifact_id=artifact["id"])
    try:
        digest = x4.unpack(gh, run, artifact, sha, folder / "candidate")
        result["firmware_sha256"] = digest
        x4.require(cam.eligible_source(gh, number) == sha, "X4 PR head moved")
        gh.call(f"/statuses/{sha}", "POST", {
            "context": "X4 hardware / trusted owner SHA", "state": "pending",
            "description": "X4 exact candidate accepted; bounded device tests pending"})
        result["device"] = runner(folder / "candidate" / x4.IMAGE, sha, digest, profile)
        if hardware_success(result):
            result["status_state"] = "success"
        elif all(result["device"].get(key) is True for key in
                 ("identity_verified", "candidate_readback_equal", "metadata_preserved", "closed")):
            result["status_state"] = "failure"
    except Exception:
        result["status_state"] = "error"
        result["error"] = "artifact_or_device_failed"  # no raw data or exception text
    # Persist before status publication. A failed status post retries just the
    # post. Crashes before this checkpoint leave an incomplete folder and stop.
    save(path, result)
    hardware_report(gh, result, path)
    return result


def hardware_once(gh, number, root, profile):
    x4.live()  # before discovery, journal mutation, status posts or device access
    from x4 import adapter
    root.mkdir(mode=0o700, parents=True, exist_ok=True)
    with (root / "hardware-scan.lock").open("a+") as lock:
        fcntl.flock(lock.fileno(), fcntl.LOCK_EX | fcntl.LOCK_NB)
        return _hardware_once(gh, number, root, profile, adapter.run)


def hardware_scan(gh, numbers, root, python, cam_binding, x4_profile, targets=TARGETS):
    """Reviewable scheduler integration; never installed/activated by this PR.

    CAM retains its original runner/journal. X4's gate is caught per target so
    an unavailable X4 never suppresses CAM or creates an X4 pass. Tests mock CAM.
    """
    x4.require(0 < len(numbers) <= cam.MAX_OWNER_PRS and targets
               and len(set(targets)) == len(targets) and set(targets) <= set(TARGETS), "Invalid hardware scan")
    root.mkdir(mode=0o700, parents=True, exist_ok=True)
    results = []
    with (root / "multi-hardware-scan.lock").open("a+") as lock:
        fcntl.flock(lock.fileno(), fcntl.LOCK_EX | fcntl.LOCK_NB)
        deadline = time.monotonic() + 1200
        for number in numbers:
            for target in targets:
                if time.monotonic() >= deadline:
                    return results
                try:
                    if target == "cam":
                        cam.once(gh, number, root, python, cam_binding)
                        results.append({"target": "cam", "result": "see_original_cam_journal"})
                    else:
                        hardware_once(gh, number, root, x4_profile)
                except cam.RateLimited:
                    return results
                except (OSError, ValueError, RuntimeError, KeyError):
                    results.append({"target": target, "result": "blocked_or_failed"})
    return results


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--targets", nargs="+", choices=TARGETS, default=list(TARGETS))
    parser.add_argument("--dry-run", action="store_true", required=True)
    group = parser.add_mutually_exclusive_group(required=True)
    group.add_argument("--pr", type=int)
    group.add_argument("--scan", action="store_true")
    parser.add_argument("--evidence-root", type=Path, required=True)
    parser.add_argument("--report", action="store_true", help="Post only explicit dry-run readiness statuses")
    args = parser.parse_args()
    # No credential lookup, configuration edits, serial imports, resets, or
    # physical commands. Missing --dry-run is rejected by argparse first.
    gh = cam.GitHub(os.environ.get("GH_TOKEN"))
    if args.scan:
        prs = gh.call("/pulls?state=open&per_page=100")
        x4.require(len(prs) < 100, "PR scan exceeds bound")
        numbers = [p["number"] for p in cam.bounded_owner_prs(prs)]
    else:
        x4.require(0 < args.pr < 1_000_000, "Invalid PR")
        numbers = [args.pr]
    if numbers:
        print(json.dumps(scan(gh, numbers, args.evidence_root, tuple(args.targets), args.report)))


if __name__ == "__main__":
    main()
