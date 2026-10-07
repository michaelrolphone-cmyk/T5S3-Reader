#!/usr/bin/env python3
"""Exercise the complete production container parser and its caller with real Expat."""
from pathlib import Path
import os
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / "lib/Epub/Epub/parsers"
STUBS = r'''
#pragma once
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <string>
#include <set>
#include <limits>
class Print {
 public:
  virtual ~Print() = default;
  virtual size_t write(uint8_t) = 0;
  virtual size_t write(const uint8_t*, size_t) = 0;
};
class Epub {
 public:
  std::string xml;
  std::string cachePath = "/cache";
  size_t failAfter = std::numeric_limits<size_t>::max();
  mutable size_t bytesWritten = 0;
  const std::string& getCachePath() const;
  std::string getCoverBmpPath(bool cropped) const;
  std::string getThumbBmpPath() const;
  std::string getThumbBmpPath(int height) const;
  size_t chunk = 512;
  bool sizeOk = true, streamOk = true;
  bool getItemSize(const char* path, size_t* size) const {
    if (!sizeOk || std::string(path) != "META-INF/container.xml") return false;
    *size = xml.size();
    return true;
  }
  bool readItemContentsToStream(const char* path, Print& sink, size_t) const {
    if (!streamOk || std::string(path) != "META-INF/container.xml") return false;
    for (size_t pos = 0; pos < xml.size();) {
      const size_t count = std::min(chunk, xml.size() - pos);
      if (sink.write(reinterpret_cast<const uint8_t*>(xml.data() + pos), count) != count) return false;
      pos += count;
      bytesWritten += count;
      if (pos >= failAfter) return false;
    }
    return true;
  }
  bool findContentOpfFile(std::string*) const;
};
struct StorageFixture {
  std::set<std::string> files;
  bool exists(const char* path) const { return files.count(path) != 0; }
};
inline StorageFixture Storage;
class CssParser {
 public:
  std::string cachePath = "/cache";
  bool hasCache() const;
};
std::string sectionPath(const Epub* epub, int spineIndex);
std::string sectionDirectory(const Epub* epub);
std::string imagePrefix(const Epub* epub, int spineIndex);
#define LOG_DBG(...) ((void)0)
#define LOG_ERR(...) ((void)0)
'''

with tempfile.TemporaryDirectory(prefix="epub-container-") as temp:
    temp = Path(temp)
    (temp / "fixtures.h").write_text(STUBS)
    for name in ["Print.h", "Logging.h"]:
        (temp / name).write_text('#include "fixtures.h"\n')
    epub = (ROOT / "lib/Epub/Epub.cpp").read_text()
    begin = epub.index("bool Epub::findContentOpfFile(")
    end = epub.index("\nbool Epub::parseContentOpf(", begin)
    # Compile unchanged getters/derived-path expressions so legacy cache fixtures
    # test the paths production consumers actually use, not duplicate test policy.
    getters = epub[epub.index("std::string Epub::getCoverBmpPath("):epub.index("\nbool Epub::generateCoverBmp(")]
    getters += epub[epub.index("std::string Epub::getThumbBmpPath()"):epub.index("\nbool Epub::generateThumbBmp(")]
    getters += "\n" + next(line for line in epub.splitlines() if line.startswith("const std::string& Epub::getCachePath()"))
    import re
    section = (ROOT / "lib/Epub/Epub/Section.cpp").read_text()
    section_header = (ROOT / "lib/Epub/Epub/Section.h").read_text()
    section_path = re.search(r"filePath\(([^\n]+)\) \{\}", section_header).group(1)
    section_dir = re.search(r"const auto sectionsDir = ([^;]+);", section).group(1)
    image_prefix = re.search(r"std::string imageBasePath = ([^;]+);", section).group(1)
    getters += f"\nstd::string sectionPath(const Epub* epub, int spineIndex) {{ return {section_path}; }}\n"
    getters += f"std::string sectionDirectory(const Epub* epub) {{ return {section_dir}; }}\n"
    getters += f"std::string imagePrefix(const Epub* epub, int spineIndex) {{ return {image_prefix}; }}\n"
    css = (ROOT / "lib/Epub/Epub/css/CssParser.cpp").read_text()
    getters += "\n" + next(line for line in css.splitlines() if line.startswith("constexpr ") and "rulesCache" in line)
    getters += "\n" + next(line for line in css.splitlines() if line.startswith("bool CssParser::hasCache()"))
    assert epub.count('Storage.removeDir((cachePath + EpubContentCache::sections).c_str())') == 2
    # This entire caller is unchanged; only ZIP/SD I/O is represented by a fixture.
    (temp / "caller.cpp").write_text('#include "fixtures.h"\n#include "ContainerParser.h"\n#include "ContentCachePaths.h"\n' + epub[begin:end] + getters)
    flags = ["-std=c++17", "-Wall", "-Wextra", "-Werror", "-g"]
    if os.environ.get("SANITIZE", "1") != "0":
        flags += ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
    binary = temp / "container_test"
    subprocess.run([os.environ.get("CXX", "c++"), *flags,
                    "-I" + str(temp), "-I" + str(SOURCE), "-I" + str(SOURCE.parent), "-I" + str(ROOT / "lib/XmlParserUtils"),
                    str(SOURCE / "ContainerParser.cpp"), str(temp / "caller.cpp"),
                    str(ROOT / "test/epub_container/container_test.cpp"),
                    "-lexpat", "-o", str(binary)], check=True, timeout=60)
    subprocess.run([str(binary)], check=True, timeout=30)
print("production EPUB default-rendition regression passed")
