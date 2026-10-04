#!/usr/bin/env python3
"""Exercise full production path helper and OPF parser against real Expat."""
from pathlib import Path
import os
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / "lib/Epub/Epub/parsers"

with tempfile.TemporaryDirectory(prefix="epub-paths-") as directory:
    out = Path(directory)
    for name in ("Print.h", "Epub.h", "Logging.h", "Serialization.h", "TestBookMetadataCache.h"):
        (out / name).write_text('#include "fixtures.h"\n')
    (out / "ContentOpfParser.h").write_text((SOURCE / "ContentOpfParser.h").read_text())
    source = (SOURCE / "ContentOpfParser.cpp").read_text()
    assert source.count('#include "../BookMetadataCache.h"') == 1
    (out / "ContentOpfParser.cpp").write_text(source.replace(
        '#include "../BookMetadataCache.h"', '#include "TestBookMetadataCache.h"'))
    flags = ["-std=c++17", "-Wall", "-Wextra", "-Werror", "-Wno-unused-variable", "-g"]
    # FsHelpers' existing sort routine has two unused locals; no unrelated edit.
    if os.environ.get("SANITIZE", "1") != "0":
        flags += ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
    binary = out / "normalise_test"
    subprocess.run([os.environ.get("CXX", "c++"), *flags,
                    "-I" + str(out), "-I" + str(ROOT / "test/epub_paths"),
                    "-I" + str(ROOT / "test/fs_helpers_test_stubs"),
                    "-I" + str(ROOT / "lib/FsHelpers"), "-I" + str(ROOT / "lib/XmlParserUtils"),
                    str(ROOT / "lib/FsHelpers/FsHelpers.cpp"),
                    str(out / "ContentOpfParser.cpp"),
                    str(ROOT / "test/epub_paths/normalise_test.cpp"),
                    "-lexpat", "-o", str(binary)], check=True, timeout=60)
    subprocess.run([str(binary), *sys.argv[1:]], check=True, timeout=30)
