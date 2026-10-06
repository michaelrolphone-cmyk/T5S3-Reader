#!/usr/bin/env python3
"""Production metadata/ZIP/parser/volume-HAL scheduling differential regression."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tempfile
import zipfile

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
BASELINE_HASH = "9f2ebe9b91e658a5aa09012c92f0057cea1e586c655eaf767bea35d91563e5dd"
BMC = "lib/Epub/Epub/BookMetadataCache.cpp"
GUARD = "#if defined(BOARD_XTEINK_X4_PRO) || defined(BOARD_T5S3_PRO)"



def ensure_baseline_git(root, commit):
    """Obtain one pinned object for depth-one CI checkouts; never skip baseline."""
    present = subprocess.run(['git', 'cat-file', '-e', commit + '^{commit}'],
                             cwd=root, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                             timeout=30, check=False)
    if present.returncode != 0:
        # Fetch only this commit, without updating branches/tags or FETCH_HEAD.
        subprocess.run(['git', 'fetch', '--depth=1', '--no-tags', '--no-write-fetch-head',
                        'origin', commit], cwd=root, check=True, timeout=120)
        subprocess.run(['git', 'cat-file', '-e', commit + '^{commit}'],
                       cwd=root, check=True, timeout=30)


def fixture(path, n, sections, method):
    opf = '<?xml version="1.0"?><package xmlns="http://www.idpf.org/2007/opf" xmlns:dc="http://purl.org/dc/elements/1.1/" version="3.0" unique-identifier="id"><metadata><dc:identifier id="id">urn:uuid:12345678-1234-5678-1234-567812345678</dc:identifier><dc:title>Reference Book</dc:title><dc:creator>Author</dc:creator><dc:language>en</dc:language><meta property="dcterms:modified">2026-10-05T00:00:00Z</meta></metadata><manifest><item id="nav" href="nav.xhtml" media-type="application/xhtml+xml" properties="nav"/>'
    opf += ''.join(f'<item id="c{i:04d}" href="ch{i:04d}.xhtml" media-type="application/xhtml+xml"/>' for i in range(n))
    opf += '</manifest><spine>' + ''.join(f'<itemref idref="c{i:04d}"/>' for i in range(n)) + '</spine></package>'
    nav = '<?xml version="1.0"?><html xmlns="http://www.w3.org/1999/xhtml" xmlns:epub="http://www.idpf.org/2007/ops"><head><title>Contents</title></head><body><nav epub:type="toc"><ol>'
    nav += ''.join(f'<li><a href="ch{i:04d}.xhtml#s{j:04d}">Topic {i:04d}.{j:04d}</a></li>' for i in range(n) for j in range(sections))
    nav += '</ol></nav></body></html>'
    chapter = '<?xml version="1.0"?><html xmlns="http://www.w3.org/1999/xhtml"><head><title>Chapter</title></head><body>' + ''.join(f'<p id="s{j:04d}">A short explanatory note.</p>' for j in range(sections)) + '</body></html>'
    with zipfile.ZipFile(path, 'w') as z:
        def put(name, contents, compression=method):
            info = zipfile.ZipInfo(name, date_time=(2026, 10, 5, 0, 0, 0))
            info.compress_type = compression
            z.writestr(info, contents)
        put('mimetype', 'application/epub+zip', 0)
        put('META-INF/container.xml', '<?xml version="1.0"?><container xmlns="urn:oasis:names:tc:opendocument:xmlns:container" version="1.0"><rootfiles><rootfile full-path="OPS/content.opf" media-type="application/oebps-package+xml"/></rootfiles></container>')
        put('OPS/content.opf', opf)
        put('OPS/nav.xhtml', nav)
        for i in range(n):
            put(f'OPS/ch{i:04d}.xhtml', chapter)
    with zipfile.ZipFile(path) as z:
        assert z.testzip() is None


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--board', choices=('x4', 't5'), default='x4')
    parser.add_argument('--sanitize', action='store_true', help='AddressSanitizer only; known ZIP BUG256 prevents a UBSan claim')
    parser.add_argument('--original-source-root', type=Path, help='offline exact baseline tree; otherwise read pinned blobs with git show')
    parser.add_argument('--negative-control', action='store_true', help='require unchanged original source to fail the optimized scheduling assertion')
    parser.add_argument('--quick', action='store_true', help='single accepted 16-spine/64-TOC archive plus all fault cases')
    args = parser.parse_args()
    data = (HERE / 'baseline.json').read_bytes()
    assert hashlib.sha256(data).hexdigest() == BASELINE_HASH
    baseline = json.loads(data)
    if not args.original_source_root:
        ensure_baseline_git(ROOT, baseline['commit'])
    original = {}
    for path, expected in baseline['source_sha256'].items():
        data = ((args.original_source_root / path).read_bytes() if args.original_source_root else
                subprocess.check_output(['git', 'show', baseline['commit'] + ':' + path], cwd=ROOT, timeout=30))
        assert hashlib.sha256(data).hexdigest() == expected, path
        original[path] = data
    production = (ROOT / BMC).read_text()
    start = production.index('bool BookMetadataCache::buildBookBin(')
    end = production.index('\nbool BookMetadataCache::cleanupTmpFiles()', start)
    operation = production[start:end]
    assert operation.count(GUARD) == 1
    control = production[:start] + operation.replace(GUARD, '#if 0 // operation-only scheduling control') + production[end:]
    variants = [('original', original[BMC].decode(), False), ('control', control, False),
                ('cooperative', production, True),
                ('legacy-guard', '#include <HalStorage.h>\n#undef BOARD_XTEINK_X4_PRO\n#undef BOARD_T5S3_PRO\n' + production, False)]
    if args.negative_control:
        variants = [('original-negative', original[BMC].decode(), True)]
    flags = ['-O1', '-g0', '-ffunction-sections', '-fdata-sections', '-Wl,--gc-sections',
             '-Wall', '-Wextra', '-Werror', '-Wno-unused-function', '-Wno-unused-variable',
             '-Wno-unused-parameter', '-Wno-sign-compare', '-Wno-overloaded-virtual',
             '-include', 'Arduino.h', '-D' + ('BOARD_XTEINK_X4_PRO' if args.board == 'x4' else 'BOARD_T5S3_PRO')]
    sanitize = ['-fsanitize=address', '-fno-omit-frame-pointer'] if args.sanitize else []
    env = dict(os.environ, ASAN_OPTIONS='detect_leaks=0')
    with tempfile.TemporaryDirectory(prefix='epub-metadata-') as directory:
        build = Path(directory)
        workloads = []
        sizes = [(16, 4)] if args.quick or args.negative_control else [(16, 4), (16, 16), (16, 64), (128, 1), (256, 4)]
        for n, sections in sizes:
            for method in ([0] if args.quick or args.negative_control else [0, 8]):
                path = build / f'book-{n}-{sections}-{method}.epub'
                fixture(path, n, sections, method)
                for chunk in ([257] if args.quick or args.negative_control else [257, 1024]):
                    workloads.append((path, n, sections, method, chunk))
        references = {}
        for name, source, optimized in variants:
            tree = build / name
            originals = name.startswith('original')
            for path in baseline['source_sha256']:
                target = tree / path
                target.parent.mkdir(parents=True, exist_ok=True)
                target.write_bytes(original[path] if originals else (ROOT / path).read_bytes())
            if not originals:
                (tree / 'lib/hal/HalWriteBudget.h').write_bytes((ROOT / 'lib/hal/HalWriteBudget.h').read_bytes())
            (tree / BMC).write_text(source)
            # Link the exact production path helper body. The rest of FsHelpers
            # requires unrelated device/UI code not reached by this operation.
            helper = (tree / 'lib/FsHelpers/FsHelpers.cpp').read_text()
            first = helper.index('std::string normalisePath(')
            last = helper.index('\n}', first) + 2
            normalise = tree / 'normalise.cpp'
            normalise.write_text('#include <vector>\n#include <string>\nnamespace FsHelpers {\n' + helper[first:last] + '\n}\n')
            includes = [HERE / 'stubs', tree / 'test/epub_anchor_reads/stubs', tree / 'test/storage_volume/stubs']
            includes += [tree / p for p in ('lib/hal', 'sdk/driver', 'lib/Epub', 'lib/Serialization', 'lib/XmlParserUtils', 'lib/ZipFile', 'lib/InflateReader')]
            obj = tree / 'tinf.o'
            subprocess.run([os.environ.get('CC', 'cc'), '-std=c11', '-O1', '-g0', '-ffunction-sections', '-fdata-sections',
                            *sanitize, '-I' + str(tree / 'lib/uzlib/src'), '-c', str(tree / 'lib/uzlib/src/tinflate.c'), '-o', str(obj)], check=True, timeout=120)
            units = [HERE / 'test.cpp', normalise, tree / 'lib/hal/HalStorageVolume.cpp', tree / BMC]
            units += [tree / 'lib/Epub/Epub/parsers' / p for p in ('ContentOpfParser.cpp', 'TocNavParser.cpp', 'ContainerParser.cpp')]
            units += [tree / 'lib/ZipFile/ZipFile.cpp', tree / 'lib/InflateReader/InflateReader.cpp', obj]
            binary = tree / 'metadata-test'
            subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++17', *flags, *sanitize,
                            '-DEXPECT_OPTIMIZED=' + str(int(optimized)), '-DHAL_HAS_WRITE_BUDGET=' + str(int(not originals)),
                            *['-I' + str(p) for p in includes], *map(str, units), '-lexpat', '-o', str(binary)], check=True, timeout=120)
            for path, n, sections, method, chunk in workloads:
                key = (n, sections, method, chunk)
                edges = key == (16, 4, 0, 257)
                output = tree / 'snapshot.bin'
                command = [str(binary), str(path), str(n), str(sections), str(chunk), str(output), 'edges' if edges else 'healthy']
                result = subprocess.run(command, env=env, text=True, capture_output=True, timeout=120)
                if args.negative_control:
                    assert result.returncode != 0 and 'metadata scheduling bound' in result.stderr, result.stderr
                    print('PASS: unchanged pinned original source independently fails optimized metadata scheduling bound')
                    return
                assert result.returncode == 0, name + ': ' + result.stderr
                print(name + f' method={method} chunk={chunk}: ' + result.stdout, end='', flush=True)
                snapshot = output.read_bytes()
                repeated = subprocess.run(command, env=env, text=True, capture_output=True, timeout=120)
                assert repeated.returncode == 0, repeated.stderr
                assert repeated.stdout == result.stdout and output.read_bytes() == snapshot, name + ': nondeterministic execution'
                digest = hashlib.sha256(snapshot).hexdigest()
                if key in references:
                    assert references[key] == digest, name + ': full provider/data/result/generation/cleanup snapshot differs for ' + str(key)
                else:
                    references[key] = digest
                print('snapshot_sha256=' + digest, flush=True)
        print('PASS: ' + args.board + (' ASan' if args.sanitize else ' normal') +
              f'; {len(workloads)} workloads, four variants, repeated exact snapshots; baseline ' + baseline['commit'])


if __name__ == '__main__':
    main()
