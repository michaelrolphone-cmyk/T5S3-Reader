#!/usr/bin/env python3
"""Exercise exact-commit acquisition using only temporary local repositories."""
import importlib.util
from pathlib import Path
import subprocess
import sys
import tempfile

sys.dont_write_bytecode = True
HERE = Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location('metadata_runner', HERE / 'run_test.py')
runner = importlib.util.module_from_spec(spec)
spec.loader.exec_module(runner)


def git(root, *args):
    return subprocess.check_output(['git', '-c', 'user.name=Regression Fixture',
                                    '-c', 'user.email=fixture@example.invalid', *args],
                                   cwd=root, stderr=subprocess.STDOUT, timeout=30)


with tempfile.TemporaryDirectory(prefix='metadata-shallow-fetch-') as temporary:
    root = Path(temporary)
    origin = root / 'origin'
    origin.mkdir()
    git(origin, 'init', '--initial-branch=main')
    (origin / 'fixture').write_text('original\n')
    git(origin, 'add', 'fixture')
    git(origin, 'commit', '-m', 'Original baseline')
    pinned = git(origin, 'rev-parse', 'HEAD').decode().strip()
    (origin / 'fixture').write_text('current\n')
    git(origin, 'commit', '-am', 'Current source')
    checkout = root / 'checkout'
    git(root, 'clone', '--depth=1', '--no-tags', origin.as_uri(), str(checkout))
    before = git(checkout, 'show-ref')
    head = git(checkout, 'rev-parse', 'HEAD')
    assert subprocess.run(['git', 'cat-file', '-e', pinned], cwd=checkout,
                          stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=30).returncode != 0
    runner.ensure_baseline_git(checkout, pinned)
    assert git(checkout, 'show', pinned + ':fixture') == b'original\n'
    assert git(checkout, 'show-ref') == before and git(checkout, 'rev-parse', 'HEAD') == head
    # Present-object path must work even when the remote is unreachable.
    git(checkout, 'remote', 'set-url', 'origin', str(root / 'missing-remote'))
    runner.ensure_baseline_git(checkout, pinned)
    assert git(checkout, 'show-ref') == before and git(checkout, 'rev-parse', 'HEAD') == head
print('PASS: bounded exact baseline fetch from shallow local clone; refs/HEAD unchanged; present-object path is offline')
