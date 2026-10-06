#!/usr/bin/env python3
"""Production image cache rendering and HAL reads; modeled storage/display boundaries."""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--sanitize", action="store_true")
parser.add_argument("--source", type=Path)
parser.add_argument("--baseline", action="store_true", help="verify original cost, not optimized bound")
args = parser.parse_args()
here = Path(__file__).resolve().parent
root = here.parents[1]
source = (args.source or root / "lib/Epub/Epub/blocks/ImageBlock.cpp").resolve()
hal = (root / "lib/hal/HalStorageVolume.cpp").read_text()
start = hal.index("int HalFile::read(void *buffer, size_t count) {")
end = hal.index("\nint HalFile::read()", start)
with tempfile.TemporaryDirectory(prefix="image-cache-reads-") as temp:
    temp = Path(temp)
    (temp / "hal.cpp").write_text('#include <HalStorage.h>\n' + hal[start:end])
    (temp / "image.cpp").write_text('''#include <cstdlib>
#include <GfxRenderer.h>
#include <Logging.h>
#include <Serialization.h>
#include "ImageBlock.h"
#include "ImageDecoderFactory.h"
#include "DirectPixelWriter.h"
#include <freertos/task.h>
void* imageTestAllocate(size_t);
void imageTestFree(void*);
#define malloc imageTestAllocate
#define free imageTestFree
''' + source.read_text().replace('"../converters/', '"'))
    flags = ["-std=c++17", "-O2", "-g", "-Wall", "-Wextra", "-Werror", "-Wno-unused-function"]
    if args.sanitize:
        flags += ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
    if args.baseline:
        flags += ["-DTEST_BASELINE=1"]
    includes = [here / "stubs", root / "lib/hal", root / "lib/Serialization",
                root / "lib/Epub/Epub", root / "lib/Epub/Epub/blocks", root / "lib/Epub/Epub/converters"]
    command = ["c++", *flags, *["-I" + str(p) for p in includes], str(temp / "image.cpp"),
               str(temp / "hal.cpp"), str(here / "image_cache_reads_test.cpp"), "-o", str(temp / "test")]
    subprocess.run(command, check=True, timeout=60)
    subprocess.run([str(temp / "test")], check=True, timeout=60, env=os.environ.copy())
