#!/usr/bin/env python3
"""Compile the real OPF parser against bounded host fixtures and system Expat.

CXX and CXXFLAGS are honored, including sanitizer flags. No sanitizer environment
variables are modified here. --source-root can point at an unchanged baseline;
that baseline must fail the reduced-read regression, not pass an alternate oracle.
"""

import argparse
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import sys
import tempfile

HERE = Path(__file__).resolve().parent
DEFAULT_ROOT = HERE.parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-root", type=Path, default=DEFAULT_ROOT)
    parser.add_argument("--source-ref", default=os.environ.get("OPF_TEST_SOURCE_REF"),
                        help="git revision for production parser cpp/h only; default working tree")
    parser.add_argument("--outputs-only", action="store_true",
                        help="emit deterministic complete healthy metadata/spine snapshots, without I/O assertions")
    args = parser.parse_args()
    root = args.source_root.resolve()
    relative_sources = (
        "lib/Epub/Epub/parsers/ContentOpfParser.cpp",
        "lib/Epub/Epub/parsers/ContentOpfParser.h",
        "lib/Serialization/Serialization.h",
        "lib/XmlParserUtils/XmlParserUtils.h",
    )
    with tempfile.TemporaryDirectory(prefix="epub-opf-index-") as temporary:
        build = Path(temporary)
        for relative in relative_sources:
            destination = build / relative
            destination.parent.mkdir(parents=True, exist_ok=True)
            if args.source_ref and relative.startswith("lib/Epub/Epub/parsers/"):
                destination.write_bytes(subprocess.check_output(
                    ["git", "-C", str(root), "show", args.source_ref + ":" + relative]))
            else:
                shutil.copyfile(root / relative, destination)
        stubs = build / "stubs"
        stubs.mkdir()
        for name in ("Print.h", "Epub.h", "FsHelpers.h", "Logging.h", "HalStorage.h"):
            (stubs / name).write_text('#include "fixtures.h"\n')
        (build / "lib/Epub/Epub/BookMetadataCache.h").write_text('#include "fixtures.h"\n')

        # Extract the complete production function verbatim; do not substitute a
        # test normalizer. Its enclosing namespace is supplied by this TU.
        source = (root / "lib/FsHelpers/FsHelpers.cpp").read_text()
        begin = source.index("std::string normalisePath(")
        end = source.index("\n}", begin) + 2
        (build / "normalise.cpp").write_text(
            '#include "fixtures.h"\nnamespace FsHelpers {\n' + source[begin:end] + "\n}\n"
        )
        executable = build / "opf-index-test"
        command = [
            *shlex.split(os.environ.get("CXX", "c++")),
            "-std=c++17", "-Wall", "-Wextra", "-Werror", "-Wno-unused-function",
            "-g", "-O1", *shlex.split(os.environ.get("CXXFLAGS", "")),
            "-I" + str(HERE), "-I" + str(stubs),
            "-I" + str(build / "lib/Epub/Epub/parsers"),
            "-I" + str(build / "lib/Serialization"),
            "-I" + str(build / "lib/XmlParserUtils"),
            str(build / relative_sources[0]), str(build / "normalise.cpp"),
            str(HERE / "tests.cpp"), "-lexpat", "-o", str(executable),
        ]
        print("Compile:", shlex.join(command), flush=True, file=sys.stderr if args.outputs_only else sys.stdout)
        subprocess.run(command, check=True, timeout=90)
        subprocess.run([str(executable), *(["--outputs-only"] if args.outputs_only else [])], check=True, timeout=60)


if __name__ == "__main__":
    main()
