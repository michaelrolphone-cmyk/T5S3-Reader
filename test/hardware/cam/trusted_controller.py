#!/usr/bin/env python3
"""Trusted, locally pinned controller. Never execute a PR checkout or artifact code on macOS.

Requires a dedicated fine-grained GitHub credential supplied in GH_TOKEN after
approval. The controller itself must be installed from a reviewed master SHA.
"""
import argparse
import hashlib
import io
import json
import os
from pathlib import Path
import re
import ssl
import subprocess
import sys
import urllib.request
import urllib.error
import zipfile

REPO = "michaelrolphone-cmyk/T5S3-Reader"
OWNER = "michaelrolphone-cmyk"
WORKFLOW = "cam-hardware-build.yml"
STATUS_CONTEXT = "CAM hardware / trusted owner SHA"
API = "https://api.github.com/repos/" + REPO
SHA = re.compile(r"[0-9a-f]{40}\Z")
MAX_OWNER_PRS = 20


class CloudBuildFailed(ValueError):
    """The exact-head cloud workflow completed without a usable success."""


class RateLimited(RuntimeError):
    """A GitHub rate limit is retryable and must not finalize a hardware SHA."""


class GitHub:
    def __init__(self, token):
        if not token:
            raise RuntimeError("GH_TOKEN absent; controller remains inactive")
        self.token = token
        # Python.org macOS builds may lack their bundled OpenSSL CA file.
        # Keep certificate verification on using the Mac's system CA bundle.
        system_ca = Path("/etc/ssl/cert.pem")
        self.ssl_context = ssl.create_default_context(
            cafile=str(system_ca) if system_ca.is_file() else None)

    def call(self, path, method="GET", body=None, limit=1_000_000):
        data = None if body is None else json.dumps(body).encode()
        req = urllib.request.Request(API + path, data=data, method=method,
            headers={"Authorization": "Bearer " + self.token,
                     "Accept": "application/vnd.github+json",
                     "X-GitHub-Api-Version": "2022-11-28",
                     "User-Agent": "riscrte-cam-trusted-controller"})
        if path.endswith("/zip"):
            class NoRedirect(urllib.request.HTTPRedirectHandler):
                def redirect_request(self, request, fp, code, msg, headers, newurl):
                    return None
            try:
                urllib.request.build_opener(
                    NoRedirect, urllib.request.HTTPSHandler(context=self.ssl_context)
                ).open(req, timeout=20)
            except urllib.error.HTTPError as redirect:
                if redirect.code in (403, 429) and (redirect.headers.get("Retry-After")
                        or redirect.headers.get("X-RateLimit-Remaining") == "0"):
                    raise RateLimited("GitHub API rate limited") from None
                if redirect.code != 302:
                    raise
                url = redirect.headers["Location"]
                if not url.startswith("https://"):
                    raise ValueError("Artifact redirect is not HTTPS")
                # Signed archive URL needs no GitHub credential. Never forward
                # Authorization to the artifact storage host.
                with urllib.request.urlopen(url, timeout=20,
                                            context=self.ssl_context) as response:
                    raw = response.read(limit + 1)
                if len(raw) > limit:
                    raise ValueError("Artifact ZIP exceeds bound")
                return raw
            raise ValueError("Artifact endpoint did not redirect")
        try:
            with urllib.request.urlopen(req, timeout=20,
                                        context=self.ssl_context) as response:
                if int(response.headers.get("Content-Length", "0")) > limit:
                    raise ValueError("GitHub response exceeds bound")
                raw = response.read(limit + 1)
        except urllib.error.HTTPError as error:
            if error.code in (403, 429) and (error.headers.get("Retry-After")
                    or error.headers.get("X-RateLimit-Remaining") == "0"):
                raise RateLimited("GitHub API rate limited") from None
            raise
        if len(raw) > limit:
            raise ValueError("GitHub response exceeds bound")
        return json.loads(raw)


def eligible_source(gh, number):
    pr = gh.call(f"/pulls/{number}")
    head = pr["head"]["sha"]
    if (pr["state"] != "open" or pr["base"]["repo"]["full_name"] != REPO
            or pr["head"]["repo"]["full_name"] != REPO
            or pr["head"]["repo"]["id"] != pr["base"]["repo"]["id"]
            or pr["user"]["login"] != OWNER or not SHA.fullmatch(head)):
        raise ValueError("PR is not an open owner-authored same-repository exact head")
    # This narrow source policy is automatic: the owner controls both the PR
    # author account and the branch in this repository. External PRs fail
    # closed. Firmware from an eligible head still runs on the CAM, so this
    # account and branch provenance are part of the device trust boundary.
    return head


def candidate(gh, number, sha):
    runs = gh.call(f"/actions/workflows/{WORKFLOW}/runs?event=pull_request&head_sha={sha}&per_page=100")
    if runs["total_count"] > 100:
        raise ValueError("Exact-head workflow run history exceeds bound")
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
        return None  # Cloud build has not completed; do not post a premature failure.
    if len(related) != 1:
        raise ValueError("Exact-head CAM cloud build is ambiguous")
    if related[0]["conclusion"] != "success":
        raise CloudBuildFailed("Exact-head CAM cloud build failed")
    run = related[0]
    artifacts = gh.call(f"/actions/runs/{run['id']}/artifacts?per_page=100")
    matching = [a for a in artifacts["artifacts"] if a["name"] == "cam-candidate-" + sha
                and not a["expired"] and a["size_in_bytes"] < 3_000_000]
    if len(matching) != 1:
        raise ValueError("Expected one bounded exact-SHA CAM candidate artifact")
    return run, matching[0]


def bounded_owner_prs(prs):
    eligible = [pr for pr in prs if pr["user"]["login"] == OWNER
                and (pr["head"].get("repo") or {}).get("full_name") == REPO]
    if len(eligible) > MAX_OWNER_PRS:
        raise RuntimeError("Owner PR scan exceeds one-minute API budget")
    return eligible


def unpack_candidate(gh, run, artifact, sha, folder):
    # The artifact endpoint redirects to a short-lived ZIP URL. urllib follows
    # it; no PR-controlled filename is used as a path.
    raw = gh.call(f"/actions/artifacts/{artifact['id']}/zip", limit=3_000_000)
    with zipfile.ZipFile(io.BytesIO(raw)) as archive:
        if set(archive.namelist()) != {"firmware.bin", "manifest.json"}:
            raise ValueError("Unexpected CAM artifact entries")
        if any(x.file_size > 2_000_000 for x in archive.infolist()):
            raise ValueError("CAM artifact entry exceeds bound")
        manifest = json.loads(archive.read("manifest.json"))
        image = archive.read("firmware.bin")
    info = manifest["firmware"]
    if (manifest["schema"] != 1 or manifest["source_sha"] != sha
            or manifest["run_id"] != run["id"]
            or manifest["run_attempt"] != run["run_attempt"]
            or info["file"] != "firmware.bin" or info["bytes"] != len(image)
            or not 0 < len(image) <= 2_000_000
            or hashlib.sha256(image).hexdigest() != info["sha256"]):
        raise ValueError("CAM artifact provenance or digest mismatch")
    folder.mkdir(mode=0o700)
    (folder / "firmware.bin").write_bytes(image)
    (folder / "manifest.json").write_text(json.dumps(manifest, sort_keys=True) + "\n")
    return info["sha256"]


def describe(result):
    parts = ["CAM " + result["status_state"]]
    if "firmware_sha256" in result:
        parts.append("firmware " + result["firmware_sha256"][:12])
    device = result.get("device", {})
    if "image_bytes" in device:
        parts.append("capture " + str(device["image_bytes"]) + " B")
    if device.get("baseline_restored"):
        parts.append("baseline restored")
    return "; ".join(parts)[:140]


def safe_success(result):
    device = result.get("device", {})
    return (result.get("result") == "pass" and result.get("device_exit") == 0
            and device.get("result") == "pass"
            and device.get("candidate_readback_equal") is True
            and device.get("baseline_restored") is True
            and SHA.fullmatch(result.get("source_sha", "")) is not None
            and re.fullmatch(r"[0-9a-f]{64}", result.get("firmware_sha256", "")) is not None)


def terminal_state(result):
    if safe_success(result):
        return "success"
    device = result.get("device", {})
    if (device.get("baseline_restored") is True
            and device.get("candidate_readback_equal") is True
            and (result.get("device_exit") != 0 or device.get("result") != "pass")):
        return "failure"
    return "error"


def post_status(gh, sha, state, description):
    if state not in {"pending", "success", "failure", "error"} or not SHA.fullmatch(sha):
        raise ValueError("Invalid exact-SHA hardware status")
    return gh.call(f"/statuses/{sha}", "POST", {
        "state": state, "context": STATUS_CONTEXT, "description": description[:140]})


def load_cam_binding(path):
    if path.is_symlink() or path.stat().st_mode & 0o077:
        raise ValueError("CAM binding must be a private non-symlink file")
    if path.stat().st_size > 1024:
        raise ValueError("CAM binding exceeds size bound")
    binding = json.loads(path.read_text())
    if (set(binding) != {"port", "location", "mac"}
            or binding["mac"] != "28:84:85:4b:57:98"
            or not isinstance(binding["port"], str)
            or re.fullmatch(r"/dev/cu\.usbserial-[0-9]+", binding["port"]) is None
            or not isinstance(binding["location"], str)
            or re.fullmatch(r"[0-9]+-[0-9]+(?:\.[0-9]+)*", binding["location"]) is None):
        raise ValueError("CAM binding identity or topology invalid")
    return binding


def once(gh, number, evidence_root, python, binding):
    sha = eligible_source(gh, number)
    result_dir = evidence_root / sha
    if result_dir.exists():
        saved = result_dir / "result.json"
        if saved.exists():
            prior = json.loads(saved.read_text())
            if prior.get("source_sha") != sha:
                raise RuntimeError("CAM journal does not match current PR head")
            if prior.get("status_id"):
                return
            if prior.get("status_state") not in {"success", "failure", "error"}:
                raise RuntimeError("Incomplete CAM result is not safe to finalize")
            if prior["status_state"] == "success" and not safe_success(prior):
                raise RuntimeError("CAM success journal lacks restoration or exact hashes")
            status = post_status(gh, sha, prior["status_state"], describe(prior))
            prior["status_id"] = status["id"]
            saved.write_text(json.dumps(prior, indent=2) + "\n")
            return
        raise RuntimeError("Incomplete private CAM journal; manual recovery required")
    try:
        found = candidate(gh, number, sha)
    except RateLimited:
        raise
    except Exception as exc:
        result_dir.mkdir(mode=0o700, parents=True)
        failed = {"schema": 1, "pr": number, "source_sha": sha,
                  "result": "failed", "status_state":
                  "failure" if isinstance(exc, CloudBuildFailed) else "error",
                  "error": f"{type(exc).__name__}: {exc}"[:300]}
        saved = result_dir / "result.json"
        saved.write_text(json.dumps(failed, indent=2) + "\n")
        status = post_status(gh, sha, failed["status_state"], describe(failed))
        failed["status_id"] = status["id"]
        saved.write_text(json.dumps(failed, indent=2) + "\n")
        return
    if found is None:
        return
    result_dir.mkdir(mode=0o700, parents=True)
    result = {"schema": 1, "pr": number, "source_sha": sha, "result": "failed"}
    try:
        run, artifact = found
        result["run_id"] = run["id"]
        artifact_dir = result_dir / "candidate"
        digest = unpack_candidate(gh, run, artifact, sha, artifact_dir)
        result["firmware_sha256"] = digest
        pending = post_status(gh, sha, "pending", "CAM hardware running; exact cloud artifact accepted")
        result["pending_status_id"] = pending["id"]
        device_dir = result_dir / "device"
        command = [str(python), str(Path(__file__).with_name("ci_device.py")),
                   "--image", str(artifact_dir / "firmware.bin"),
                   "--sha256", digest, "--out", str(device_dir),
                   "--port", binding["port"], "--location", binding["location"]]
        child_env = os.environ.copy()
        child_env.pop("GH_TOKEN", None)
        completed = subprocess.run(command, timeout=600, capture_output=True, text=True,
                                   env=child_env)
        result["device_exit"] = completed.returncode
        if (device_dir / "result.json").exists():
            device = json.loads((device_dir / "result.json").read_text())
            result["device"] = device
        if completed.returncode != 0 or result.get("device", {}).get("result") != "pass":
            raise RuntimeError("CAM device suite failed; inspect private local evidence")
        result["result"] = "pass"
        if not safe_success(result):
            raise RuntimeError("CAM success lacks verified baseline restoration")
        result["status_state"] = terminal_state(result)
    except Exception as exc:
        result["error"] = f"{type(exc).__name__}: {exc}"[:300]
        result["status_state"] = terminal_state(result)
    finally:
        # Status API errors remain visible; a missing final status is never success.
        saved = result_dir / "result.json"
        saved.write_text(json.dumps(result, indent=2) + "\n")
        status = post_status(gh, sha, result["status_state"], describe(result))
        result["status_id"] = status["id"]
        saved.write_text(json.dumps(result, indent=2) + "\n")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    group = parser.add_mutually_exclusive_group(required=True)
    group.add_argument("--pr", type=int)
    group.add_argument("--scan", action="store_true")
    parser.add_argument("--evidence-root", type=Path, required=True)
    parser.add_argument("--cam-binding", type=Path, required=True,
                        help="Private, physically mapped CAM port/location/MAC JSON")
    parser.add_argument("--python", type=Path, default=Path(sys.executable))
    args = parser.parse_args()
    token = os.environ.get("GH_TOKEN")
    if token is None:
        # Approval-time setup stores a narrowly scoped token in the login
        # keychain. Its bytes stay in memory and are never printed or persisted.
        found = subprocess.run(["/usr/bin/security", "find-generic-password", "-w",
                                "-s", "riscrte-cam-ci", "-a", OWNER],
                               capture_output=True, text=True, timeout=10)
        if found.returncode == 0:
            token = found.stdout.strip()
    gh = GitHub(token)
    binding = load_cam_binding(args.cam_binding)
    if args.scan:
        prs = gh.call("/pulls?state=open&per_page=100")
        if len(prs) == 100:
            raise RuntimeError("Open PR scan exceeds one-page bound")
        for pr in bounded_owner_prs(prs):
            try:
                once(gh, pr["number"], args.evidence_root, args.python, binding)
            except ValueError as exc:
                print(f"PR {pr['number']}: {exc}", file=sys.stderr)
    else:
        if not 0 < args.pr < 1_000_000:
            raise ValueError("Invalid PR number")
        once(gh, args.pr, args.evidence_root, args.python, binding)


if __name__ == "__main__":
    main()
