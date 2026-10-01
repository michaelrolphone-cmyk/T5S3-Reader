#!/usr/bin/env python3
"""Compile and run the real Button Remap app and bridge, with host services.

--source-ref permits regression checks against an existing local commit without
modifying the worktree. Only the app and bridge are read from that revision.
"""
import argparse
import os
from pathlib import Path
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source-ref', help='local revision containing production sources')
    parser.add_argument('--case', choices=('all', 'bridge', 'app', 'chrome'), default='all')
    args = parser.parse_args()

    with tempfile.TemporaryDirectory(prefix='button-remap-test-') as directory:
        build = Path(directory)
        sources = []
        for relative in ('Apps/button_remap.c', 'src/native/NativeButtonRemapBridge.cpp'):
            source = ROOT / relative
            if args.source_ref:
                source = build / Path(relative).name
                source.write_bytes(subprocess.check_output(
                    ['git', 'show', f'{args.source_ref}:{relative}'], cwd=ROOT, timeout=30))
            sources.append(source)

        flags = ['-O1', '-g', '-Wall', '-Wextra', '-Werror',
                 '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
                 '-fno-pie', f'-I{ROOT / "lib/NativeApps/include"}',
                 f'-I{ROOT / "test/native_apps/button_remap_stubs"}']
        app_object = build / 'button_remap.o'
        subprocess.run(shlex.split(os.environ.get('CC', 'cc')) + ['-std=c11'] + flags +
                       ['-c', str(sources[0]), '-o', str(app_object)], check=True, timeout=60)
        binary = build / 'button_remap_test'
        subprocess.run(shlex.split(os.environ.get('CXX', 'c++')) + ['-std=c++17'] + flags +
                       ['-no-pie', str(sources[1]),
                        str(ROOT / 'test/native_apps/button_remap_runtime_test.cpp'),
                        str(app_object), '-o', str(binary)], check=True, timeout=60)
        env = os.environ.copy()
        # LeakSanitizer cannot run under ptrace in managed host test executors.
        # Address/undefined sanitizers stay enabled, including fail-fast UBSan.
        env['ASAN_OPTIONS'] = env.get('ASAN_OPTIONS', '') + ':detect_leaks=0'
        env['UBSAN_OPTIONS'] = env.get('UBSAN_OPTIONS', '') + ':halt_on_error=1'
        subprocess.run([str(binary), args.case], check=True, timeout=30, env=env)


if __name__ == '__main__':
    main()
