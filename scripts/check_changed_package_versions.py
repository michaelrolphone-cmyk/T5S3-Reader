#!/usr/bin/env python3
"""Refuse changed distributable package sources with unchanged versions.

Compare the candidate tree with a supplied Git base commit. The release index
still enforces published identity collisions; this check catches source edits
before the build produces new bytes under an old version.
"""
from __future__ import annotations

import argparse
import json
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[1]
VERSION = re.compile(r"(?:0|[1-9][0-9]*)\.(?:0|[1-9][0-9]*)\.(?:0|[1-9][0-9]*)\Z")
KINDS = ("Drivers", "Services", "Providers")


def git(*args: str) -> bytes:
    return subprocess.check_output(("git", *args), cwd=ROOT, stderr=subprocess.DEVNULL)


def old_bytes(base: str, path: Path) -> bytes | None:
    try:
        return git("show", f"{base}:{path.as_posix()}")
    except subprocess.CalledProcessError:
        return None


def version(body: bytes, path: Path) -> tuple[int, int, int]:
    data = json.loads(body)
    value = data.get("version") if isinstance(data, dict) else None
    if not isinstance(value, str) or not VERSION.fullmatch(value):
        raise ValueError(f"{path}: invalid package version")
    return tuple(map(int, value.split(".")))


def check(base: str, zip_transition: bool = False) -> list[str]:
    git("cat-file", "-e", f"{base}^{{commit}}")
    changed = {Path(p.decode()) for p in git("diff", "--name-only", "--no-renames", base, "--", "Apps", *KINDS).splitlines()}
    failures = []
    for path in sorted(ROOT.joinpath("Apps").glob("*.json")):
        rel = path.relative_to(ROOT)
        previous = old_bytes(base, rel)
        if previous is None:
            continue
        # Every app is a ZIP identity in the U1 transition, including apps
        # whose C source did not otherwise change.
        changed_payload = zip_transition or any(
            p.parent == Path("Apps") and
            (p.stem == rel.stem or p.stem.startswith(rel.stem + "_"))
            for p in changed
        )
        if changed_payload and version(path.read_bytes(), rel) <= version(previous, rel):
            failures.append(f"{rel}: changed app/ZIP source requires a higher version")
    for kind in KINDS:
        for path in sorted(ROOT.joinpath(kind).glob("*/manifest.json")):
            rel = path.relative_to(ROOT)
            previous = old_bytes(base, rel)
            if previous is None:
                continue
            if any(p.parts[:2] == rel.parts[:2] for p in changed):
                if version(path.read_bytes(), rel) <= version(previous, rel):
                    failures.append(f"{rel}: changed package source requires a higher version")
    return failures


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--base", required=True, help="candidate's target commit")
    parser.add_argument("--zip-transition", action="store_true", help="require every existing app to advance past loose distribution")
    args = parser.parse_args()
    problems = check(args.base, args.zip_transition)
    if problems:
        parser.exit(1, "\n".join(problems) + "\n")
    print("Changed package source versions: PASS")
