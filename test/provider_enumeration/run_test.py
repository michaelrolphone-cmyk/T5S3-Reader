#!/usr/bin/env python3
"""Run the actual production metadata enumerator with faultable HalStorage.

--source can point to a captured original InstalledProviderGraph.cpp to prove
its warm-query regression. No historical checkout is needed in normal CI.
"""
from pathlib import Path
import argparse
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser()
parser.add_argument("--source", type=Path)
parser.add_argument("--sanitize", action="store_true")
args = parser.parse_args()
source = (args.source or ROOT / "src/runtime/drivers/InstalledProviderGraph.cpp").read_text()
helpers = source[source.index("bool pathFor("):source.index("bool parseExactImports(")]
start = source.index("bool nextProvider(")
function = source[start:source.index("\nbool acquire(", start)]
harness = (ROOT / "test/provider_enumeration/prefix.cpp").read_text() + helpers + function
harness += (ROOT / "test/provider_enumeration/cases.cpp").read_text()
with tempfile.TemporaryDirectory() as temporary:
    build = Path(temporary)
    (build / "test.cpp").write_text(harness)
    flags = ["-std=c++17", "-O1", "-g", "-Wall", "-Wextra", "-Werror", "-Wno-overloaded-virtual",
             "-DARDUINO_ARCH_ESP32", "-Itest/provider_enumeration/stubs", "-Itest/hal/storage_stubs",
             "-Ilib/hal", "-Isrc", "-Isdk/driver"]
    if args.sanitize:
        flags += ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
    subprocess.run(["c++", *flags, "lib/hal/HalStorage.cpp", str(build / "test.cpp"),
                    "-o", str(build / "test")], cwd=ROOT, check=True, timeout=60)
    for extra in ([], ["close-failure"]):
        subprocess.run([str(build / "test"), *extra], check=True, timeout=30)
