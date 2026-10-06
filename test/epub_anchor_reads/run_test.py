#!/usr/bin/env python3
"""Complete production Section/parser/layout and full volume HAL differential test.

Only ZIP extraction, fonts, scheduler/mutex and the memory volume provider are
fixtures. No SD transport, device timing, bundled Expat or legacy SdFat claim.
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
BASELINE_HASH = "6d522a3be9d83848b16a17a9372b20f111e45d30bb87f12a28bf9feb04d95f36"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sanitize", action="store_true")
    parser.add_argument("--board", choices=("x4", "t5"), default="x4")
    parser.add_argument("--verify-baseline-git", action="store_true")
    parser.add_argument("--original-source-root", type=Path,
                        help="also compare exact pinned original Section/HAL/Serialization files")
    parser.add_argument("--negative-control", action="store_true",
                        help="the original lookup must fail optimized scheduling assertions")
    args = parser.parse_args()
    fixture = (HERE / "baseline.json").read_bytes()
    assert hashlib.sha256(fixture).hexdigest() == BASELINE_HASH
    old = json.loads(fixture)
    original_sources = {}
    if args.verify_baseline_git or args.original_source_root:
        for path, expected in old["source_sha256"].items():
            source = ((args.original_source_root / path).read_bytes() if args.original_source_root else
                      subprocess.check_output(["git", "show", old["commit"] + ":" + path], cwd=ROOT))
            assert hashlib.sha256(source).hexdigest() == expected, path
            original_sources[path] = source.decode()
    section = (ROOT / "lib/Epub/Epub/Section.cpp").read_text()
    start = section.index("std::optional<uint16_t> Section::getPageForAnchor(")
    end = section.index("\nstd::optional<uint16_t> Section::getPageForParagraphIndex(", start)
    anchor = section[start:end]
    guard = "#if defined(BOARD_XTEINK_X4_PRO) || defined(BOARD_T5S3_PRO)"
    assert anchor.count(guard) == 1, "expected exactly one selected reader guard"
    control = section[:start] + anchor.replace(guard, "#if 0 // reader-only scheduling control") + section[end:]
    board = "BOARD_XTEINK_X4_PRO" if args.board == "x4" else "BOARD_T5S3_PRO"
    flags = ["-std=c++17", "-O1", "-g0", "-Wall", "-Wextra", "-Werror",
             "-Wno-unused-parameter", "-Wno-unused-function", "-Wno-reorder",
             "-Wno-sign-compare", "-Wno-unused-variable", "-Wno-unused-but-set-variable",
             "-Wno-parentheses", "-Wno-overloaded-virtual", "-D" + board, "-include", "Arduino.h"]
    if args.sanitize:
        flags += ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
    env = dict(os.environ, ASAN_OPTIONS="detect_leaks=0", UBSAN_OPTIONS="halt_on_error=1")
    units = [ROOT / "lib/Epub/Epub" / p for p in
             ("Page.cpp", "ParsedText.cpp", "blocks/TextBlock.cpp", "parsers/ChapterHtmlSlimParser.cpp",
              "htmlEntities.cpp", "css/CssParser.cpp")]
    units += [ROOT / "lib/Utf8/Utf8.cpp", HERE / "test.cpp"]
    with tempfile.TemporaryDirectory(prefix="epub-anchor-") as directory:
        build = Path(directory)
        includes = [HERE / "stubs", build, ROOT / "test/storage_volume/stubs", ROOT / "lib/hal",
                    ROOT / "sdk/driver", ROOT / "lib/Epub", ROOT / "lib/Epub/Epub",
                    ROOT / "lib/Utf8", ROOT / "lib/XmlParserUtils"]
        reference = None
        variants = [("control", control, False), ("cooperative", section, True),
                    ("legacy-guard", "#include <HalStorage.h>\n#undef BOARD_XTEINK_X4_PRO\n"
                     "#undef BOARD_T5S3_PRO\n" + section, False)]
        if original_sources:
            variants.append(("original", original_sources["lib/Epub/Epub/Section.cpp"], False))
        if args.negative_control:
            variants = [("negative-control", control, True)]
        for name, source, optimized in variants:
            (build / "Section.cpp").write_text(source)
            (build / "Serialization.h").write_text(original_sources["lib/Serialization/Serialization.h"] if name == "original" else
                                                   (ROOT / "lib/Serialization/Serialization.h").read_text())
            hal = build / "HalStorageVolume.cpp"
            hal.write_text(original_sources["lib/hal/HalStorageVolume.cpp"] if name == "original" else
                           (ROOT / "lib/hal/HalStorageVolume.cpp").read_text())
            binary = build / "test"
            subprocess.run([os.environ.get("CXX", "c++"), *flags, "-DEXPECT_OPTIMIZED=" + str(int(optimized)),
                            "-DHAL_HAS_COOPERATIVE=" + str(int(name != "original")),
                            *["-I" + str(p) for p in includes], str(build / "Section.cpp"), str(hal),
                            *map(str, units), "-lexpat", "-o", str(binary)], check=True, timeout=120)
            result = subprocess.run([str(binary), str(build / "snapshot.bin")], env=env,
                                    text=True, capture_output=True, timeout=120)
            print(name + ": " + result.stdout, end="", flush=True)
            if name == "negative-control":
                assert result.returncode != 0 and "scheduling bound" in result.stderr, result.stderr
                print("PASS: original lookup independently fails the optimized scheduling bound")
                return
            assert result.returncode == 0, result.stderr
            snapshot = (build / "snapshot.bin").read_bytes()
            if optimized:
                repeated = subprocess.run([str(binary), str(build / "repeat.bin")], env=env,
                                          text=True, capture_output=True, timeout=120)
                assert repeated.returncode == 0, repeated.stderr
                assert repeated.stdout == result.stdout and (build / "repeat.bin").read_bytes() == snapshot, \
                    "optimized workload is nondeterministic"
                (build / "repeat.bin").unlink()
            if reference is None:
                reference = snapshot
            else:
                assert snapshot == reference, name + ": exact semantic/provider/cleanup trace differs"
            binary.unlink()
        print("PASS: " + "/".join(name for name, _, _ in variants) + " exact snapshot equality; SHA256 " + hashlib.sha256(reference).hexdigest())


if __name__ == "__main__":
    main()
