#!/usr/bin/env python3
"""Pinned multi-target candidate scanner. X4 has no live execution path.

This entry point validates cloud artifacts only. The existing CAM controller
and its live timer remain unchanged. Install separately only after review.
"""
import argparse
from contextlib import contextmanager
import fcntl
import hashlib
import json
import os
from pathlib import Path
import sys
import time
import zipfile

sys.path.insert(0, str(Path(__file__).resolve().parent / "cam"))
import trusted_controller as cam
import ci_device as cam_device
from x4 import contract as x4

TARGETS = ("cam", "x4")


@contextmanager
def locks(port, mac, directory=None):
    """Use existing lab directory/hash keys, including both macOS aliases.

    Acquires all keys before any caller I/O, nonblocking, releasing partial
    acquisition on contention. Never unlink a lock file while peers may use it.
    """
    directory = cam_device.LOCK_DIR if directory is None else directory
    directory.mkdir(mode=0o700, parents=True, exist_ok=True)
    aliases = {port, port.replace("/dev/tty.", "/dev/cu."), port.replace("/dev/cu.", "/dev/tty.")}
    handles = []
    try:
        for key in sorted({"mac:" + mac.lower()} | {"port:" + p for p in aliases}):
            handle = (directory / (hashlib.sha256(key.encode()).hexdigest() + ".lock")).open("a+")
            handles.append(handle)
            fcntl.flock(handle.fileno(), fcntl.LOCK_EX | fcntl.LOCK_NB)
        yield
    finally:
        for handle in reversed(handles):
            handle.close()


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
