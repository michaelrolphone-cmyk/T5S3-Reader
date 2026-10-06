#!/usr/bin/env python3
"""Real TXT paging + read window + HAL/FatFs/SD model, not device timing."""
from pathlib import Path
import os
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'test/storage_volume'))
from mmio_boundary import header

with tempfile.TemporaryDirectory() as tmp:
    build = Path(tmp)
    (build / 'x4pro_mmio.h').write_text(header(ROOT))
    # Arduino unsigned long is32bits on ESP32, unlike this64bit host.
    arduino = (ROOT / 'test/storage_volume/stubs/Arduino.h').read_text()
    arduino = arduino.replace('inline unsigned long millis() { return static_cast<unsigned long>(card_time); }',
                              'inline uint32_t millis() { return static_cast<uint32_t>(card_time); }')
    (build / 'Arduino.h').write_text(arduino)
    source = (ROOT / 'test/storage_volume/runtime_test.cpp').read_text().split('#include "directory_iteration_test.inc"')[0]
    txt = (ROOT / 'lib/Txt/Txt.cpp').read_text()
    source += '\n#include <Txt.h>\n#include <algorithm>\n#include <Logging.h>\n'
    source += txt[txt.index('Txt::Txt('):txt.index('std::string Txt::getTitle')]
    source += """
static bool failWindowAllocation;
static unsigned windowAllocations, windowFrees;
static size_t lastWindowAllocation;
static void* windowMalloc(size_t bytes) {
    assert(bytes >= 1 && bytes <= 8193);
    lastWindowAllocation = bytes;
    if (failWindowAllocation) return nullptr;
    ++windowAllocations; return malloc(bytes);
}
static void windowFree(void* p) { if (p) ++windowFrees; free(p); }
#define malloc windowMalloc
#define free windowFree
"""
    source += txt[txt.index('bool Txt::readContent('):]
    source += '\n#undef malloc\n#undef free\n'
    paging = (ROOT / 'src/activities/reader/TxtReaderPaging.cpp').read_text()
    source += (ROOT / 'test/txt_index/facade.h').read_text()
    source += paging[paging.index('namespace {'):paging.index('int TxtReaderActivity::lineHeightFor')]
    activity = (ROOT / 'src/activities/reader/TxtReaderActivity.cpp').read_text()
    method = activity[activity.index('bool TxtReaderActivity::buildPageIndex()'):activity.index('void TxtReaderActivity::render(')]
    if '--original' in sys.argv:
        method = method.replace('fenceAfter, &window)', 'fenceAfter)')
    source += method
    source += (ROOT / 'test/txt_index/cases.cpp').read_text()
    (build / 'test.cpp').write_text(source)
    flags = ['-fsanitize=address,undefined', '-fno-omit-frame-pointer'] if '--sanitize' in sys.argv else []
    objects = []
    for path in ('Drivers/x4pro_sd/driver.c', 'Drivers/storage_fatfs/fatfs/ff.c',
                 'Drivers/storage_fatfs/fatfs/ffunicode.c', 'test/storage_volume/os_cpu_fake.c'):
        obj = build / (Path(path).name + '.o')
        subprocess.run(['cc', '-std=c11', '-O1', '-g', *flags, '-Wall', '-Wextra', '-Werror', '-Wno-overflow',
                        '-pthread', '-D_XOPEN_SOURCE=700', '-Isdk/driver', '-I' + str(build),
                        '-IDrivers/x4pro_board', '-c', path, '-o', str(obj)], cwd=ROOT, check=True)
        objects.append(str(obj))
    binary = build / 'txt'
    subprocess.run(['c++', '-std=c++17', '-O1', '-g', *flags, '-Wall', '-Wextra', '-Werror',
                    '-Wno-overloaded-virtual', '-pthread', '-DBOARD_XTEINK_X4_PRO',
                    '-I' + str(build), '-Itest/storage_volume/stubs', '-Itest/storage_volume', '-Ilib/hal',
                    '-Ilib/Markdown', '-Ilib/Txt', '-Isdk/driver', '-I' + str(build),
                    str(build / 'test.cpp'), 'lib/hal/HalStorageVolume.cpp', 'lib/Markdown/Markdown.cpp',
                    *objects, '-o', str(binary)], cwd=ROOT, check=True)
    env = dict(os.environ, UBSAN_OPTIONS='halt_on_error=1')
    subprocess.run([str(binary)], cwd=ROOT, env=env, check=True, timeout=180)
