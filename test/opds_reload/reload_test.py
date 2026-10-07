#!/usr/bin/env python3
"""Compile complete production store/bridge and exact JSON loader with host I/O fixtures."""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser()
parser.add_argument('arduino_json', type=Path)
parser.add_argument('--source-ref', help='Original source for a negative control')
parser.add_argument('--sanitize', action='store_true')
args = parser.parse_args()

def source(path):
    if args.source_ref:
        return subprocess.check_output(['git', 'show', f'{args.source_ref}:{path}'], cwd=ROOT, text=True)
    return (ROOT / path).read_text()

with tempfile.TemporaryDirectory(prefix='opds-reload-') as tmp:
    out = Path(tmp)
    (out / 'OpdsServerStore.cpp').write_text(source('src/OpdsServerStore.cpp'))
    (out / 'NativeOpdsBridge.cpp').write_text(source('src/native/NativeOpdsBridge.cpp'))
    json = source('src/JsonSettingsIO.cpp')
    start = json.index('bool JsonSettingsIO::loadOpds(')
    end = json.index('\n// ---- Bookmarks ----', start)
    (out / 'load_opds.inc').write_text(json[start:end])
    command = [os.environ.get('CXX', 'c++'), '-std=c++17', '-Wall', '-Wextra', '-Werror',
               '-I' + str(ROOT / 'test/opds_reload/stubs'), '-I' + str(out),
               '-I' + str(args.arduino_json.resolve()), '-I' + str(ROOT / 'src'),
               '-I' + str(ROOT / 'lib/NativeApps/include'), str(out / 'OpdsServerStore.cpp'),
               str(out / 'NativeOpdsBridge.cpp'), str(ROOT / 'test/opds_reload/reload_test.cpp'),
               '-o', str(out / 'test')]
    if args.sanitize:
        command[1:1] = ['-g', '-fsanitize=address,undefined', '-fno-omit-frame-pointer']
    subprocess.run(command, check=True, timeout=60)
    subprocess.run([str(out / 'test')], check=True, timeout=30)
