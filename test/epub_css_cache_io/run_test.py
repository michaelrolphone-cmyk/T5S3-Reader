#!/usr/bin/env python3
"""Complete production CSS cache + volume HAL differential host regression.

Memory volume, heap, clock and mutex endpoints are fixtures. No device claim.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tempfile

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
BASELINE_HASH = "2706d9795225bc6bdd33035a6027becfcaecbb6b1b33d4ee576d3b4df2d78519"
CSS = "lib/Epub/Epub/css/CssParser.cpp"
HAL = "lib/hal/HalStorageVolume.cpp"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sanitize", action="store_true")
    parser.add_argument("--board", choices=("x4", "t5"), default="x4")
    parser.add_argument("--verify-baseline-git", action="store_true")
    parser.add_argument("--original-source-root", type=Path,
                        help="also build the exact independently pinned original CSS/HAL sources")
    parser.add_argument("--negative-control", action="store_true",
                        help="the original reader/writer must fail optimized scheduling assertions")
    args = parser.parse_args()
    fixture = (HERE / "baseline.json").read_bytes()
    assert hashlib.sha256(fixture).hexdigest() == BASELINE_HASH
    baseline = json.loads(fixture)
    originals = {}
    if args.verify_baseline_git or args.original_source_root:
        for path, expected in baseline["source_sha256"].items():
            data = ((args.original_source_root / path).read_bytes() if args.original_source_root else
                    subprocess.check_output(["git", "show", baseline["commit"] + ":" + path], cwd=ROOT))
            assert hashlib.sha256(data).hexdigest() == expected, path
            originals[path] = data
    production = (ROOT / CSS).read_text()
    start = production.index("bool CssParser::saveToCache() const {")
    guard = "#if defined(BOARD_XTEINK_X4_PRO) || defined(BOARD_T5S3_PRO)"
    assert production[start:].count(guard) == 2, "expected only the two selected cache-operation guards"
    control = production[:start] + production[start:].replace(guard, "#if 0 // cache-only scheduling control")
    variants = [("control", control, False), ("cooperative", production, True),
                ("legacy-guard", "#include <HalStorage.h>\n#undef BOARD_XTEINK_X4_PRO\n"
                 "#undef BOARD_T5S3_PRO\n" + production, False)]
    if originals:
        variants.append(("original", originals[CSS].decode(), False))
    if args.negative_control:
        variants = [("negative-write", control, True), ("negative-read", control, True)]
    flags = ["-std=c++17", "-O1", "-g0", "-Wall", "-Wextra", "-Werror",
             "-Wno-unused-parameter", "-Wno-unused-function", "-Wno-sign-compare",
             "-Wno-unused-variable", "-Wno-overloaded-virtual", "-include", "Arduino.h",
             "-D" + ("BOARD_XTEINK_X4_PRO" if args.board == "x4" else "BOARD_T5S3_PRO")]
    if args.sanitize:
        flags += ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
    env = dict(os.environ, ASAN_OPTIONS="detect_leaks=0", UBSAN_OPTIONS="halt_on_error=1")
    with tempfile.TemporaryDirectory(prefix="epub-css-cache-") as directory:
        build = Path(directory)
        reference = None
        for name, source, optimized in variants:
            for path in baseline["source_sha256"]:
                target = build / path
                target.parent.mkdir(parents=True, exist_ok=True)
                target.write_bytes(originals[path] if name == "original" else (ROOT / path).read_bytes())
            (build / CSS).write_text(source)
            includes = [ROOT / "test/epub_anchor_reads/stubs", ROOT / "test/storage_volume/stubs",
                        build / "lib/hal", ROOT / "lib/hal", ROOT / "sdk/driver", build / "lib/Epub"]
            binary = build / "test"
            subprocess.run([os.environ.get("CXX", "c++"), *flags,
                            "-DEXPECT_OPTIMIZED=" + str(int(optimized)),
                            "-DHAL_HAS_WRITE_BUDGET=" + str(int(name != "original")),
                            "-DNEGATIVE_READ_ONLY=" + str(int(name == "negative-read")),
                            *["-I" + str(p) for p in includes], str(build / CSS), str(build / HAL),
                            str(HERE / "test.cpp"), "-o", str(binary)], check=True, timeout=120)
            result = subprocess.run([str(binary), str(build / "snapshot.bin")], env=env,
                                    text=True, capture_output=True, timeout=120)
            print(name + ": " + result.stdout, end="", flush=True)
            if name.startswith("negative-"):
                operation = name.removeprefix("negative-")
                assert result.returncode != 0 and operation + " scheduling bound" in result.stderr, result.stderr
                print("PASS: original cache " + operation + " independently fails its optimized scheduling bound")
                continue
            assert result.returncode == 0, result.stderr
            snapshot = (build / "snapshot.bin").read_bytes()
            repeated = subprocess.run([str(binary), str(build / "repeat.bin")], env=env,
                                      text=True, capture_output=True, timeout=120)
            assert repeated.returncode == 0, repeated.stderr
            assert repeated.stdout == result.stdout and (build / "repeat.bin").read_bytes() == snapshot, \
                name + ": workload is nondeterministic"
            if reference is None:
                reference = snapshot
            else:
                assert snapshot == reference, name + ": exact provider/data/result/generation/cleanup trace differs"
        if args.negative_control:
            return
        print("PASS: " + "/".join(name for name, _, _ in variants) +
              " exact snapshot equality; SHA256 " + hashlib.sha256(reference).hexdigest())


if __name__ == "__main__":
    main()
