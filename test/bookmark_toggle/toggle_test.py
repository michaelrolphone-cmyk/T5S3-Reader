"""Execute production bookmark toggle, mapper, XPath resolver, ZIP and Expat.

Section/page, metadata, storage, clock, rendering and persistence are host fixtures.
BOOKMARK_TOGGLE_ACTIVITY can point to an original activity source for negative and
snapshot comparisons. BOOKMARK_TOGGLE_BASELINE=1 relaxes only the new cost bound.
"""
from pathlib import Path
import json
import os
import subprocess
import tempfile
import zipfile

ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent
BASELINE = os.environ.get('BOOKMARK_TOGGLE_BASELINE') == '1'
SANITIZE = os.environ.get('BOOKMARK_TOGGLE_SANITIZE') == '1'
ACTIVITY = Path(os.environ.get('BOOKMARK_TOGGLE_ACTIVITY', ROOT / 'src/activities/reader/EpubReaderActivity.cpp'))

with tempfile.TemporaryDirectory(prefix='bookmark-toggle-') as directory:
    build = Path(directory)
    (build / 'freertos').mkdir()
    (build / 'freertos/FreeRTOS.h').write_text('#pragma once\n#include <cstdint>\n#include <cstdlib>\n')
    (build / 'freertos/task.h').write_text('#pragma once\nvoid vTaskDelay(int);\n')
    (build / 'Arduino.h').write_text('#pragma once\n#include <cstdint>\nuint32_t millis();\nvoid vTaskDelay(int);\n')
    (build / 'Logging.h').write_text('#pragma once\ntemplate<class...T> inline void logDiscard(T&&...){}\n#define LOG_ERR(...) logDiscard(__VA_ARGS__)\n#define LOG_DBG(...) logDiscard(__VA_ARGS__)\n#define LOG_INF(...) logDiscard(__VA_ARGS__)\n')
    (build / 'FsHelpers.h').write_text('#pragma once\n#include <string>\nnamespace FsHelpers{std::string normalisePath(const std::string&);}\n')
    storage = (ROOT / 'test/epub_spine_sizes/HalStorage.h').read_text()
    storage = storage.replace('class Print { public: virtual ~Print()=default; virtual size_t write(const uint8_t*,size_t)=0; };', '#include "Print.h"')
    storage = storage.replace('struct StorageFixture {', 'extern unsigned mkdirCalls;\nextern bool mkdirResult;\nstruct StorageFixture {\n bool mkdir(const char*) {++mkdirCalls;return mkdirResult;}')
    (build / 'HalStorage.h').write_text(storage)
    source = (ROOT / 'lib/FsHelpers/FsHelpers.cpp').read_text()
    start = source.index('std::string normalisePath(')
    (build / 'normalise.inc').write_text(source[start:source.index('\n}', start) + 2])
    source = ACTIVITY.read_text()
    start = source.index('struct ProgressRange {')
    (build / 'range.inc').write_text(source[start:source.index('\nstd::string extractPageText', start)])
    start = source.index('void EpubReaderActivity::addBookmark()')
    (build / 'bookmark.inc').write_text(source[start:source.index('\nScreenshotInfo ', start)])
    source = (ROOT / 'lib/Epub/Epub.cpp').read_text()
    parts = []
    for signature in ('bool Epub::readItemContentsToStream(', 'bool Epub::getItemSize(', 'float Epub::calculateProgress('):
        start = source.index(signature)
        parts.append(source[start:source.index('\n}', start) + 2])
    (build / 'epub.inc').write_text('\n'.join(parts))
    source = (ROOT / 'lib/ZipFile/ZipFile.cpp').read_text()
    assert source.count('out.write(buffer, dataRead)') == 1
    assert source.count('out.write(outputBuffer, produced)') == 1
    source = source.replace('out.write(buffer, dataRead)', 'countOutput(out, buffer, dataRead)')
    source = source.replace('out.write(outputBuffer, produced)', 'countOutput(out, outputBuffer, produced)')
    prefix = '#include <Print.h>\nextern unsigned long long deliveredBytes,deliveryCalls;\nstatic size_t countOutput(Print& out,const uint8_t* b,size_t n){deliveredBytes+=n;++deliveryCalls;return out.write(b,n);}\n'
    (build / 'ZipFile.cpp').write_text(prefix + source)
    flags = ['-O1', '-g', '-fno-omit-frame-pointer', '-ffunction-sections', '-fdata-sections', '-Wall', '-Wextra', '-Werror']
    # Existing canonical BUG256 owns ZIP EOCD unaligned typed loads; use ASan,
    # not a misleading all-UBSan claim or suppression in production code.
    if SANITIZE:
        flags += ['-fsanitize=address']
    objects = []
    c_sources = [ROOT / 'lib/uzlib/src/tinflate.c'] + [ROOT / 'lib/expat' / (name + '.c') for name in ('xmlparse', 'xmlrole', 'xmltok')]
    for i, path in enumerate(c_sources):
        obj = build / f'{i}.o'
        subprocess.run([os.environ.get('CC', 'cc'), '-std=c11', *flags, *(['-Wno-comment'] if path.parent.name == 'expat' else []), '-DXML_GE=0', '-DXML_CONTEXT_BYTES=1024', '-I' + str(path.parent), '-c', str(path), '-o', str(obj)], check=True)
        objects.append(obj)
    includes = [build, HERE, ROOT / 'src/util', ROOT / 'lib/KOReaderSync', ROOT / 'lib/ZipFile', ROOT / 'lib/InflateReader', ROOT / 'lib/uzlib/src', ROOT / 'lib/Utf8', ROOT / 'lib/XmlParserUtils', ROOT / 'lib/Epub', ROOT / 'lib/expat']
    sources = [HERE / 'toggle_test.cpp', build / 'ZipFile.cpp', ROOT / 'lib/InflateReader/InflateReader.cpp', ROOT / 'lib/KOReaderSync/ProgressMapper.cpp', ROOT / 'lib/KOReaderSync/ChapterXPathResolver.cpp', ROOT / 'lib/Utf8/Utf8.cpp', ROOT / 'src/util/BookmarkUtil.cpp', ROOT / 'lib/Epub/Epub/htmlEntities.cpp']
    exe = build / 'test'
    subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++17', *flags, *(['-DBASELINE_SOURCE'] if BASELINE else []), '-include', 'cstdlib', *['-I' + str(p) for p in includes], *map(str, sources + objects), '-Wl,--gc-sections', '-o', str(exe)], check=True)
    rows = []
    for size in (16384, 131072, 1048576):
        prefix = b'<?xml version="1.0"?><html><body><p>First paragraph.</p>'
        suffix = b'</body></html>'
        sentence = b'<p>This is an ordinary paragraph with several readable words and a stable bookmark location.</p>'
        remaining = size - len(prefix) - len(suffix)
        chapter = prefix + sentence * (remaining // len(sentence)) + b' ' * (remaining % len(sentence)) + suffix
        for method in (0, 8):
            archive = build / 'book.epub'
            with zipfile.ZipFile(archive, 'w', compression=method) as z:
                z.writestr('OPS/ch.xhtml', chapter)
            for indexed in (0, 1):
                output = subprocess.check_output([str(exe), str(archive), str(size), str(indexed), 'full' if size == 16384 else 'basic'], text=True, timeout=45)
                for line in output.splitlines():
                    row = json.loads(line)
                    row.update(size=size, method=method, indexed=indexed)
                    rows.append(row)
    # An XML error must retain the existing addition fallback, while removal
    # must not parse the malformed chapter at all.
    for method in (0, 8):
        archive = build / 'malformed.epub'
        chapter = b'<html><body><broken!' + b'x' * 16384
        with zipfile.ZipFile(archive, 'w', compression=method) as z:
            z.writestr('OPS/ch.xhtml', chapter)
        for indexed in (0, 1):
            output = subprocess.check_output([str(exe), str(archive), str(len(chapter)), str(indexed), 'malformed'], text=True, timeout=45)
            for line in output.splitlines():
                row = json.loads(line)
                row.update(size=len(chapter), method=method, indexed=indexed, malformed=True)
                rows.append(row)
    destination = os.environ.get('BOOKMARK_TOGGLE_RESULTS')
    if destination:
        Path(destination).write_text(json.dumps(rows, indent=2))
    label = 'baseline comparison' if BASELINE else 'zero-chapter-work removal'
    print(f'PASS: {len(rows)} production bookmark actions, {label}; addition, matching, failures, retry and cleanup checked')
