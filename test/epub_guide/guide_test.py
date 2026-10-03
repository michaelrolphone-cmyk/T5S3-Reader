#!/usr/bin/env python3
"""Run the complete production OPF parser with real Expat and in-memory SD fixtures."""
from pathlib import Path
import os
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / "lib/Epub/Epub/parsers"
STUBS = r'''
#pragma once
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#include <expat.h>
class Print {
 public:
  virtual ~Print() = default;
  virtual size_t write(uint8_t) = 0;
  virtual size_t write(const uint8_t*, size_t) = 0;
};
struct FsFile {
  std::shared_ptr<std::vector<uint8_t>> data;
  size_t pos = 0;
  explicit operator bool() const { return data != nullptr; }
  void close() { data.reset(); pos = 0; }
  size_t position() const { return pos; }
  void seek(size_t value) { pos = value; }
  bool available() const { return data && pos < data->size(); }
};
struct StorageFixture {
  std::unordered_map<std::string, std::shared_ptr<std::vector<uint8_t>>> files;
  bool openFileForWrite(const char*, const std::string& path, FsFile& file) {
    file.close();
    file.data = files[path] = std::make_shared<std::vector<uint8_t>>();
    return true;
  }
  bool openFileForRead(const char*, const std::string& path, FsFile& file) {
    file.close();
    auto it = files.find(path);
    if (it == files.end()) return false;
    file.data = it->second;
    return true;
  }
  bool exists(const char* path) const { return files.count(path) != 0; }
  void remove(const char* path) { files.erase(path); }
};
inline StorageFixture Storage;
namespace serialization {
template <typename T> void writePod(FsFile& f, const T& value) {
  const auto* bytes = reinterpret_cast<const uint8_t*>(&value);
  f.data->insert(f.data->end(), bytes, bytes + sizeof(T));
  f.pos = f.data->size();
}
template <typename T> void readPod(FsFile& f, T& value) {
  if (!f || f.pos + sizeof(T) > f.data->size()) std::abort();
  std::memcpy(&value, f.data->data() + f.pos, sizeof(T));
  f.pos += sizeof(T);
}
inline void writeString(FsFile& f, const std::string& s) {
  if (!f) return;
  for (char c : s) f.data->push_back(static_cast<uint8_t>(c));
  f.data->push_back(0); f.pos = f.data->size();
}
inline void readString(FsFile& f, std::string& s) {
  s.clear();
  while (f.available()) { char c = (*f.data)[f.pos++]; if (!c) break; s += c; }
}
}
namespace FsHelpers {
// These fixtures use canonical paths; path normalization is outside this test.
inline std::string normalisePath(const std::string& s) { return s; }
}
class BookMetadataCache {
 public:
  struct Metadata { std::string title, author, language, coverItemHref, textReferenceHref; } coreMetadata;
  std::string cachePath = "/cache";
  FsFile bookFile;
  size_t lutOffset = 0;
  uint16_t spineCount = 0, tocCount = 0;
  bool loaded = false;
  std::vector<std::string> spine;
  void createSpineEntry(const std::string& href) { spine.push_back(href); }
  bool load();
};
#define LOG_DBG(...) ((void)0)
#define LOG_ERR(...) ((void)0)
'''

with tempfile.TemporaryDirectory(prefix="epub-guide-") as temp:
    temp = Path(temp)
    (temp / "fixtures.h").write_text(STUBS)
    for name in ["Print.h", "Epub.h", "FsHelpers.h", "Logging.h", "Serialization.h", "TestBookMetadataCache.h"]:
        (temp / name).write_text('#include "fixtures.h"\n')
    # Preserve the complete production class and implementation. Redirect only
    # the platform storage/cache include; no callback or parser logic is copied.
    (temp / "ContentOpfParser.h").write_text((SOURCE / "ContentOpfParser.h").read_text())
    source = (SOURCE / "ContentOpfParser.cpp").read_text()
    assert source.count('#include "../BookMetadataCache.h"') == 1
    (temp / "ContentOpfParser.cpp").write_text(source.replace(
        '#include "../BookMetadataCache.h"', '#include "TestBookMetadataCache.h"'))
    cache_source = (ROOT / "lib/Epub/Epub/BookMetadataCache.cpp").read_text()
    load_begin = cache_source.index("bool BookMetadataCache::load() {")
    load_end = cache_source.index("\nBookMetadataCache::SpineEntry", load_begin)
    cache_version = re.search(r"constexpr uint8_t BOOK_CACHE_VERSION = \d+;", cache_source).group(0)
    # Exercise the unchanged production cache loader, including close-on-old-version.
    (temp / "cache_load.cpp").write_text('#include "fixtures.h"\n' + cache_version +
        '\nconstexpr char bookBinFile[] = "/book.bin";\n' + cache_source[load_begin:load_end])
    flags = ["-std=c++17", "-Wall", "-Wextra", "-Werror", "-g"]
    if os.environ.get("SANITIZE", "1") != "0":
        flags += ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
    binary = temp / "guide_test"
    subprocess.run([os.environ.get("CXX", "c++"), *flags,
                    "-I" + str(temp), "-I" + str(ROOT / "lib/XmlParserUtils"),
                    str(temp / "ContentOpfParser.cpp"), str(temp / "cache_load.cpp"),
                    str(ROOT / "test/epub_guide/guide_test.cpp"),
                    "-lexpat", "-o", str(binary)], check=True, timeout=60)
    subprocess.run([str(binary)], check=True, timeout=30)
print("production EPUB guide parser regression passed")
