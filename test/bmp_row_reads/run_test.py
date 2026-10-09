#!/usr/bin/env python3
"""Production Bitmap/helpers, renderer bitmap loops and volume-HAL row reads.

Storage, clock, allocation and pixel sinks are explicit test boundaries. --source-root
allows an independent unchanged checkout; --baseline checks its original wait cost.
"""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--sanitize", action="store_true")
parser.add_argument("--baseline", action="store_true")
parser.add_argument("--legacy", action="store_true")
parser.add_argument("--t5", action="store_true")
parser.add_argument("--source-root", type=Path)
args = parser.parse_args()
here = Path(__file__).resolve().parent
root = here.parents[1]
source = (args.source_root or root).resolve()
renderer = (source / "lib/GfxRenderer/GfxRenderer.cpp").read_text()
renderer = renderer[renderer.index("void GfxRenderer::drawBitmap("):renderer.index("void GfxRenderer::fillPolygon(")]
hal = (source / "lib/hal/HalStorageVolume.cpp").read_text()
hal = hal[hal.index("int HalFile::read(void *buffer, size_t count) {"):hal.index("\nint HalFile::read()")]
with tempfile.TemporaryDirectory(prefix="bmp-row-reads-") as directory:
    directory = Path(directory)
    (directory / "renderer.cpp").write_text('''#include <GfxRenderer.h>
#include <Bitmap.h>
#include <HalReadBudget.h>
#include <Logging.h>
#include <freertos/task.h>
#include <cmath>
void* rowAllocate(size_t);
void rowFree(void*);
#define malloc rowAllocate
#define free rowFree
''' + renderer)
    (directory / "hal.cpp").write_text('#include <HalStorage.h>\n' + hal)
    flags = ["-std=c++17", "-O1", "-g", "-Wall", "-Wextra", "-Werror"]
    if not args.legacy:
        flags += ["-DBOARD_T5S3_PRO" if args.t5 else "-DBOARD_XTEINK_X4_PRO"]
    if args.baseline or args.legacy:
        flags += ["-DTEST_BASELINE=1"]
    if args.sanitize:
        flags += ["-fsanitize=address,undefined", "-fno-sanitize-recover=all", "-fno-omit-frame-pointer"]
    includes = [here / "stubs", source / "lib/GfxRenderer", root / "lib/hal"]
    command = ["c++", *flags, *["-I" + str(p) for p in includes], str(directory / "renderer.cpp"),
               str(directory / "hal.cpp"), str(source / "lib/GfxRenderer/Bitmap.cpp"),
               str(source / "lib/GfxRenderer/BitmapHelpers.cpp"), str(here / "test.cpp"),
               "-o", str(directory / "test")]
    subprocess.run(command, check=True, timeout=120)
    subprocess.run([str(directory / "test")], check=True, timeout=120, env=os.environ.copy())
