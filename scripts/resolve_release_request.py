#!/usr/bin/env python3
"""Validate a RiscRTE release request and emit GitHub Actions job outputs."""
import configparser
import json
import os
import re
import subprocess
from pathlib import Path

def resolve(event_name, event, root):
    if event_name == "workflow_dispatch":
        inputs = event.get("inputs", {})
        tag = inputs.get("tag", "")
        commit = inputs.get("commit_firmware", True)
        if isinstance(commit, str):
            if commit not in ("true", "false"):
                raise ValueError("commit_firmware must be a boolean")
            commit = commit == "true"
        enabled = True
    elif event_name == "push":
        request = json.loads((root / ".github/release-request.json").read_text())
        if set(request) != {"enabled", "tag", "commit_firmware"}:
            raise ValueError("Request must contain enabled, tag, and commit_firmware only")
        enabled, tag, commit = (request[k] for k in ("enabled", "tag", "commit_firmware"))
    else:
        raise ValueError("Unsupported event")
    if type(enabled) is not bool or type(commit) is not bool or not isinstance(tag, str):
        raise ValueError("Invalid release request field types")
    if not enabled:
        return {"publish": "false", "tag": "", "ver": "", "commit_firmware": "false"}
    if not re.fullmatch(r"v?(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)(?:-[0-9A-Za-z]+(?:[.-][0-9A-Za-z]+)*)?", tag):
        raise ValueError("Tag must be a version such as v1.0.12")
    tag = tag if tag.startswith("v") else "v" + tag
    version = tag[1:]
    config = configparser.ConfigParser(interpolation=None, strict=False, inline_comment_prefixes=(";", "#"))
    config.read(root / "platformio.ini")
    if config.get("riscrte", "version") != version:
        raise ValueError("Requested tag must match [riscrte] version in platformio.ini")
    return {"publish": "true", "tag": tag, "ver": version, "commit_firmware": str(commit).lower()}

def main():
    outputs = resolve(os.environ["GITHUB_EVENT_NAME"],
                      json.loads(Path(os.environ["GITHUB_EVENT_PATH"]).read_text()), Path("."))
    if outputs["publish"] == "true":
        result = subprocess.run(["git", "show-ref", "--verify", "--quiet", "refs/tags/" + outputs["tag"]])
        if result.returncode == 0:
            raise ValueError("Tag already exists; choose a new version")
        if result.returncode != 1:
            raise RuntimeError("Could not check existing tags")
    with open(os.environ["GITHUB_OUTPUT"], "a") as stream:
        for key, value in outputs.items():
            stream.write(f"{key}={value}\n")
    print("RiscRTE release request validated." if outputs["publish"] == "true" else "Release publishing disabled; setup validation complete.")

if __name__ == "__main__":
    main()
