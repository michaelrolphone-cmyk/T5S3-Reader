#!/usr/bin/env python3
"""Compile production startup allocations with the engine's actual sizes."""
from pathlib import Path
import os
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
engine = (ROOT / 'Apps/hollow_trail_engine.inc').read_text()
# Keep the production size expressions, rather than duplicating numeric budgets.
size_prefix = engine[:engine.index('#define HT_GOAL')]
defines = '\n'.join(line for line in size_prefix.splitlines() if line.startswith('#define '))
app = (ROOT / 'Apps/hollow_trail.c').read_text()
packed = next(line for line in app.splitlines() if line.startswith('#define HT_PACKED_BYTES '))
for required in ('ht_startup_memory_open(&buffers,', 'ht_startup_memory_close(&buffers,',
                 'ht_bind(arena);', 'ht_native_a=buffers.data[HT_MEM_NATIVE_A]',
                 'ht_native_b=buffers.data[HT_MEM_NATIVE_B]',
                 'ht_reader_bitmap=buffers.data[HT_MEM_READER]'):
    assert required in app, required
assert 'psram_alloc(HT_MEMORY+HT_NATIVE_MEMORY+' not in app
with tempfile.TemporaryDirectory(prefix='hollow-memory-') as directory:
    sizes = Path(directory) / 'sizes.h'
    binary = Path(directory) / 'test'
    sizes.write_text(defines + '\n' + packed + '\n')
    compiler = shlex.split(os.environ.get('CC', 'cc'))
    subprocess.run(compiler + ['-std=c11', '-Wall', '-Wextra', '-Werror',
        '-fsanitize=address,undefined', '-fno-omit-frame-pointer', '-include', str(sizes),
        str(ROOT / 'test/native_apps/hollow_trail_memory_test.c'), '-o', str(binary)],
        check=True, timeout=60)
    subprocess.run([str(binary)], check=True, timeout=30)
