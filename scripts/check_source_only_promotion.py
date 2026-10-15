#!/usr/bin/env python3
"""Recognize one exact source-only master promotion, without remote access."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[1]
REPOSITORY = 'michaelrolphone-cmyk/T5S3-Reader'
BEFORE = 'b5a9887e16ce04baecdfa8366ff7d67a2a22c3cc'
SOURCE = '40ee294e8d16280f14bbe56a92103e6038a316ea'
MANIFEST = 'docs/native-sdk61-source-promotion.json'
GUARD_PATHS = frozenset({
    '.github/workflows/release.yml',
    '.github/workflows/shared-sdk61.yml',
    'scripts/check_source_only_promotion.py',
    'test/source_only_promotion_test.py',
    MANIFEST,
    'docs/NATIVE_SOURCE_PROMOTION.md',
})


def require(condition, message):
    if not condition:
        raise ValueError(message)


def expected_manifest():
    return {'schema': 1, 'repository': REPOSITORY, 'branch': 'master',
            'expected_before': BEFORE, 'source_sha': SOURCE,
            'intent': 'source-only-sdk61-promotion'}


def parse_manifest(raw):
    def unique(pairs):
        result = {}
        for key, value in pairs:
            require(key not in result, 'duplicate promotion manifest field')
            result[key] = value
        return result
    value = json.loads(raw, object_pairs_hook=unique)
    require(isinstance(value, dict) and type(value.get('schema')) is int
            and value == expected_manifest(), 'promotion manifest identity differs')
    return value


def git(root, *arguments):
    return subprocess.check_output(['git', '-C', str(root), *arguments], text=True).strip()


def evaluate(root, environ):
    require(environ.get('GITHUB_REPOSITORY') == REPOSITORY, 'wrong promotion repository')
    event = environ.get('GITHUB_EVENT_NAME')
    result = {'schema': 1, 'source_only': False, 'repository': REPOSITORY,
              'event': event, 'ref': environ.get('GITHUB_REF'),
              'before': environ.get('PROMOTION_BEFORE', ''),
              'after': environ.get('GITHUB_SHA', ''), 'reason': 'ordinary-release-routing'}
    if event == 'workflow_dispatch':
        return result
    require(event == 'push', 'unsupported release event')
    require(environ.get('GITHUB_REF') == 'refs/heads/master', 'wrong promotion branch')
    before, after = result['before'], result['after']
    require(re.fullmatch('[0-9a-f]{40}', before) and re.fullmatch('[0-9a-f]{40}', after),
            'exact push commit identities required')
    # A consumed one-time range never suppresses a later ordinary push.
    if before != BEFORE:
        return result
    require(git(root, 'rev-parse', 'HEAD') == after, 'checkout differs from push after')
    require(not git(root, 'status', '--porcelain', '--untracked-files=no'),
            'promotion checkout has tracked modifications')
    require(git(root, 'show', '-s', '--format=%P', after).split() == [SOURCE],
            'promotion must be the direct single-parent source successor')
    require(git(root, 'merge-base', BEFORE, SOURCE) == BEFORE,
            'promotion source lost current master ancestry')
    changed = set(git(root, 'diff', '--name-only', SOURCE, after).splitlines())
    require(changed == GUARD_PATHS, 'promotion successor has unexpected or missing guard paths')
    for path in GUARD_PATHS:
        entry = git(root, 'ls-tree', after, '--', path).split()
        require(len(entry) == 4 and entry[0:2] == ['100644', 'blob'] and entry[3] == path,
                'promotion guard input is not a regular tracked file')
    raw = subprocess.check_output(['git', '-C', str(root), 'show', after + ':' + MANIFEST])
    parse_manifest(raw)
    result.update(source_only=True, reason='exact-source-only-sdk61-promotion',
                  source_sha=SOURCE, changed_paths=sorted(changed),
                  manifest_sha256=hashlib.sha256(raw).hexdigest())
    return result


def write_result(result, audit_path, plan_path, github_output):
    audit_path.write_text(json.dumps(result, indent=2) + '\n')
    if result['source_only']:
        plan_path.write_text('[]\n')
    with github_output.open('a') as output:
        output.write('source_only=' + str(result['source_only']).lower() + '\n')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--audit-file', type=Path, required=True)
    parser.add_argument('--plan-file', type=Path, required=True)
    parser.add_argument('--github-output', type=Path, required=True)
    args = parser.parse_args()
    result = evaluate(ROOT, os.environ)
    write_result(result, args.audit_file, args.plan_file, args.github_output)
    print(json.dumps(result, sort_keys=True))


if __name__ == '__main__':
    main()
