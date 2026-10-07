#!/usr/bin/env python3
"""Compile production TOC callers/parsers with in-memory storage/cache boundaries."""
from pathlib import Path
import os
import re
import subprocess
import tempfile

ROOT = Path(os.environ.get("EPUB_TEST_SOURCE_ROOT", Path(__file__).resolve().parents[2]))
TEST_ROOT = Path(__file__).resolve().parents[2]


def function(source, signature):
    start = source.index(signature)
    opening = source.index('{', start)
    depth = 1
    end = opening + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


with tempfile.TemporaryDirectory(prefix='epub-toc-') as tmp:
    out = Path(tmp)
    (out / 'Print.h').write_text('''#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
class Print { public: virtual ~Print() = default;
virtual size_t write(uint8_t) = 0;
virtual size_t write(const uint8_t*, size_t) = 0; };
''')
    (out / 'Logging.h').write_text('#pragma once\n#define LOG_DBG(...) ((void)0)\n#define LOG_ERR(...) ((void)0)\n')
    (out / 'FsHelpers.h').write_text('#pragma once\n#include <string>\nnamespace FsHelpers { std::string normalisePath(const std::string&); }\n')
    fs = (ROOT / 'lib/FsHelpers/FsHelpers.cpp').read_text()
    (out / 'path.cpp').write_text('#include <string>\n#include <vector>\nnamespace FsHelpers {\n' + function(fs, 'std::string normalisePath(') + '\n}\n')
    for name in ('TocNcxParser', 'TocNavParser'):
        src = ROOT / 'lib/Epub/Epub/parsers' / name
        (out / (name + '.h')).write_text(src.with_suffix('.h').read_text())
        (out / (name + '.cpp')).write_text(src.with_suffix('.cpp').read_text().replace('../BookMetadataCache.h', 'cache.h'))
    (out / 'cache.h').write_text('''#pragma once
#include <string>
#include <vector>
struct Entry { std::string label, href, anchor; uint8_t depth; };
class BookMetadataCache { public:
std::vector<Entry> entries;
bool failReset = false;
bool resetTocEntries() { if (failReset) return false; entries.clear(); return true; }
void createTocEntry(const std::string& label, const std::string& href,
const std::string& anchor, uint8_t depth) { entries.push_back({label, href, anchor, depth}); }
};
''')
    epub = (ROOT / 'lib/Epub/Epub.cpp').read_text()
    methods = '\n'.join(function(epub, 'bool Epub::' + n + '() const') for n in ('parseTocNcxFile', 'parseTocNavFile'))
    # Exercise the actual EPUB 3 preference / EPUB 2 fallback dispatch too.
    dispatch = epub[epub.index('  bool tocParsed = false;'):epub.index('  if (!tocParsed) {')]
    (out / 'callers.inc').write_text(methods + '\nbool Epub::parseToc() const {\n' + dispatch + '\nreturn tocParsed;\n}\n')
    cache = (ROOT / 'lib/Epub/Epub/BookMetadataCache.cpp').read_text()
    version = re.search(r'constexpr uint8_t BOOK_CACHE_VERSION = \d+;', cache).group()
    # Run the production cache version rejection branch. Remaining metadata I/O
    # is outside this test; admission must close and reject old cached TOCs.
    guard = cache[cache.index('  uint8_t version;', cache.index('bool BookMetadataCache::load()')):
                  cache.index('  serialization::readPod(bookFile, lutOffset);')]
    (out / 'cache_guard.inc').write_text(version + '\n' + '''
struct VersionFile { uint8_t value; bool closed = false; void close() { closed = true; } };
namespace serialization { void readPod(VersionFile& file, uint8_t& value) { value = file.value; } }
bool admitCachedToc(VersionFile& bookFile) {
''' + guard + '\nreturn true;\n}\n')
    flags = os.environ.get('CXXFLAGS', '').split()
    command = [os.environ.get('CXX', 'c++'), '-std=c++17', '-Wall', '-Wextra', '-Werror', *flags,
               '-I' + str(out), '-I' + str(ROOT / 'lib/XmlParserUtils'),
               str(TEST_ROOT / 'test/epub_toc/path_test.cpp'), str(out / 'path.cpp'),
               str(out / 'TocNcxParser.cpp'), str(out / 'TocNavParser.cpp'), '-lexpat', '-o', str(out / 'test')]
    subprocess.run(command, check=True, timeout=60)
    subprocess.run([str(out / 'test')], check=True, timeout=30)

    # Check the actual failed-TOC discard operation independently of parser fixtures.
    if 'bool BookMetadataCache::resetTocEntries()' in cache:
        (out / 'cache_reset.inc').write_text(function(cache, 'bool BookMetadataCache::resetTocEntries()'))
        subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++17', '-Wall', '-Wextra', '-Werror',
                        *flags, '-I' + str(out), str(TEST_ROOT / 'test/epub_toc/reset_test.cpp'),
                        '-o', str(out / 'reset_test')], check=True, timeout=60)
        subprocess.run([str(out / 'reset_test')], check=True, timeout=30)
