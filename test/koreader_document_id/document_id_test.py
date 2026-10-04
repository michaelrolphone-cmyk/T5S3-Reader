#!/usr/bin/env python3
"""Compile complete production ID code; fixture SD/MD5 endpoint, no device I/O.

Healthy sampled bytes/offsets are exact, including >1 GiB synthetic files without
large allocations. This verifies error propagation, not the MD5 implementation.
"""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile

parser = argparse.ArgumentParser()
parser.add_argument("--sanitize", action="store_true")
parser.add_argument("--case", choices=("seek", "negative", "zero", "short"))
args = parser.parse_args()
ROOT = Path(os.environ.get("KOREADER_DOCUMENT_TEST_ROOT", Path(__file__).resolve().parents[2]))
HERE = Path(__file__).resolve().parent
sync = (ROOT / "src/activities/reader/KOReaderSyncActivity.cpp").read_text()
start = sync.index("  // Calculate document hash", sync.index("void KOReaderSyncActivity::performSync()"))
end = sync.index('  LOG_DBG("KOSync", "Document hash:', start)
with tempfile.TemporaryDirectory(prefix="koreader-document-id-") as temp:
    # Verbatim dispatch and failure guard, before any remote progress request.
    (Path(temp) / "sync_dispatch.inc").write_text(sync[start:end])
    binary = Path(temp) / "test"
    flags = [os.environ.get("CXX", "c++"), "-std=c++17", "-Wall", "-Wextra", "-Werror"]
    if args.sanitize:
        flags += ["-fsanitize=address,undefined", "-fno-sanitize-recover=all", "-fno-omit-frame-pointer"]
    command = flags + ["-I" + temp, "-I" + str(HERE / "stubs"), "-I" + str(ROOT / "lib/KOReaderSync"),
                       str(ROOT / "lib/KOReaderSync/KOReaderDocumentId.cpp"),
                       str(HERE / "cases.cpp"), "-o", str(binary)]
    subprocess.run(command, check=True, timeout=90)
    subprocess.run([str(binary)] + ([args.case] if args.case else []), check=True, timeout=30)
