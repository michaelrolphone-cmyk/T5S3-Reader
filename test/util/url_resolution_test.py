#!/usr/bin/env python3
"""Compile the real URL helper and browser methods; HTTP/parser/UI/storage are fixtures."""
import argparse
import os
from pathlib import Path
import shlex
import subprocess
import tempfile

HERE = Path(__file__).resolve().parent
ROOT = Path(os.environ.get("URL_SOURCE_ROOT", HERE.parents[1]))


def method(source, signature):
    start = source.index(signature)
    cursor = source.index("{", start) + 1
    depth = 1
    while depth:
        depth += (source[cursor] == "{") - (source[cursor] == "}")
        cursor += 1
    return source[start:cursor]


parser = argparse.ArgumentParser()
parser.add_argument("--sanitize", action="store_true")
parser.add_argument("--test", choices=("all", "helper", "flow"), default="all")
args = parser.parse_args()
source = (ROOT / "src/activities/browser/OpdsBookBrowserActivity.cpp").read_text()
methods = "\n\n".join(method(source, f"void OpdsBookBrowserActivity::{name}(") for name in
                      ("onExit", "fetchFeed", "navigateToEntry", "navigateBack", "downloadBook"))
with tempfile.TemporaryDirectory(prefix="opds-url-test-") as directory:
    temp = Path(directory)
    flow = temp / "flow.cpp"
    flow.write_text((HERE / "opds_url_flow_test.cpp").read_text().replace("// PRODUCTION_METHODS", methods))
    tests = {"helper": HERE / "url_resolution_test.cpp", "flow": flow}
    for name, test in tests.items():
        if args.test not in ("all", name):
            continue
        binary = temp / test.stem
        command = shlex.split(os.environ.get("CXX", "c++")) + [
            "-std=c++17", "-Wall", "-Wextra", "-Werror", "-I", str(ROOT / "src"),
            str(test), str(ROOT / "src/util/UrlUtils.cpp"), "-o", str(binary)]
        if args.sanitize:
            command += ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
        subprocess.run(command, check=True)
        subprocess.run([str(binary)], check=True, timeout=20)
