#!/usr/bin/env python3
"""Real C apps + production UI event/hit-test functions; no destructive I/O.

Rendering, button edges and hardware polling are fixtures, not hardware proof.
The UI functions are extracted verbatim so the test cannot silently substitute
row-hit semantics for the firmware's actual confirmation-control dispatch.
APP_SOURCE_ROOT may point at the baseline checkout for original-fails evidence.
"""
from pathlib import Path
import os
import subprocess
import tempfile
repo = Path(__file__).resolve().parents[2]
source_root = Path(os.environ.get('APP_SOURCE_ROOT', repo))
source = (repo / 'src/native/NativeUiBridge.cpp').read_text()
parts = [source[source.index('Rect rotatePortraitRectToCurrentOrientation('):source.index('void drawChrome(')],
         source[source.index('int32_t hitTest('):source.index('int32_t nextIndex(')]]
with tempfile.TemporaryDirectory() as tmp:
    tmp = Path(tmp)
    (tmp / 'ui_functions.inc').write_text('\n'.join(parts))
    flags = ['-Wall', '-Wextra', '-Werror', '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
             '-I' + str(repo / 'lib/NativeApps/include')]
    objects = []
    for app in ('clear_cache', 'ota_update'):
        obj = tmp / (app + '.o')
        subprocess.run([os.environ.get('CC', 'cc'), '-std=c11', *flags,
                        '-Dapp_main=' + app + '_main', '-c', str(source_root / 'Apps' / (app + '.c')),
                        '-o', str(obj)], check=True)
        objects.append(str(obj))
    exe = tmp / 'test'
    subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++17', *flags, '-I' + str(tmp),
                    str(repo / 'test/native_apps/confirmation_input_test.cpp'), *objects, '-o', str(exe)], check=True)
    subprocess.run([str(exe), os.environ.get('CONFIRMATION_CASE', 'all')], check=True, timeout=30)
