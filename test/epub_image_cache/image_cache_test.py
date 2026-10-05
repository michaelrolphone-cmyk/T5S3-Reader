#!/usr/bin/env python3
"""Link actual ImageBlock.cpp and DirectPixelWriter against bounded host fixtures."""
import argparse
from pathlib import Path
import subprocess
import re
import tempfile

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--sanitize", action="store_true")
parser.add_argument("--source", type=Path, help="alternate ImageBlock.cpp for the original-behavior negative control")
args = parser.parse_args()
root = Path(__file__).resolve().parents[2]
here = Path(__file__).resolve().parent
source = args.source or root / "lib/Epub/Epub/blocks/ImageBlock.cpp"
# Compile the real image-path selection branch and section invalidation methods.
# CSS parsing, path normalization, parser surrounding state and storage are fixtures;
# this is a source-backed caller/lifecycle check, not full EPUB XML/ZIP decoding.
parser_source = (root / "lib/Epub/Epub/parsers/ChapterHtmlSlimParser.cpp").read_text()
section_source = (root / "lib/Epub/Epub/Section.cpp").read_text()
selection_start = parser_source.index("      // Skip image if CSS display:none")
selection_end = parser_source.index("            // Extract image to cache file", selection_start)
selection = parser_source[selection_start:selection_end]
load_start = section_source.index("bool Section::loadSectionFile(")
load_end = section_source.index("// Your updated class method", load_start)
clear_start = section_source.index("bool Section::clearCache() const")
clear_end = section_source.index("bool Section::createSectionFile(", clear_start)
version = re.search(r"constexpr uint8_t SECTION_FILE_VERSION = \d+;", section_source).group()
image_base = re.search(r"  std::string imageBasePath = [^;]+;", section_source).group()
fixture = r"""
#include <Logging.h>
#include "ContentCachePaths.h"
struct FixtureEpub { std::string getCachePath() const { return "/epub"; } };
std::string productionImageBase(int spineIndex) {
  FixtureEpub storage; auto* epub = &storage;
@IMAGE_BASE@
  return imageBasePath;
}
enum class CssDisplay { Normal, None };
struct CssStyle {
  CssDisplay display = CssDisplay::Normal;
  bool hasDisplay() const { return display == CssDisplay::None; }
  void applyOver(const CssStyle& other) { if (other.hasDisplay()) display = other.display; }
};
struct CssParser {
  CssStyle resolveStyle(const char*, const std::string&) const { return {}; }
  static CssStyle parseInlineStyle(const std::string& text) {
    return {text == "display:none" ? CssDisplay::None : CssDisplay::Normal};
  }
};
namespace FsHelpers { std::string normalisePath(const std::string& path) { return path; } }
struct ParserFixture {
  CssParser* cssParser = nullptr;
  int skipUntilDepth = 0, depth = 0, imageRendering = 0, imageCounter = 0;
  std::string contentBase = "/book/", imageBasePath, selected;
};
void selectImage(ParserFixture* self, const std::string& src,
                 const std::string& classAttr, const std::string& styleAttr) {
  self->selected.clear();
@SELECTION@
            self->selected = cachedImagePath;
          }
        }
      }
}
class Section {
 public:
  FsFile file;
  std::string filePath = "/epub/sections/0.bin";
  uint16_t pageCount = 0;
  bool clearCache() const;
  bool loadSectionFile(int, float, bool, uint8_t, uint16_t, uint16_t, bool, bool, uint8_t);
};
@VERSION@
@LOAD@
@CLEAR@
"""
fixture = fixture.replace("@IMAGE_BASE@", image_base).replace("@SELECTION@", selection)
fixture = fixture.replace("@VERSION@", version).replace("@LOAD@", section_source[load_start:load_end])
fixture = fixture.replace("@CLEAR@", section_source[clear_start:clear_end])
with tempfile.TemporaryDirectory(prefix="epub-image-cache-") as temporary:
    binary = Path(temporary) / "image_cache_test"
    (Path(temporary) / "caller_production.inc").write_text(fixture)
    command = ["c++", "-std=c++17", "-Wall", "-Wextra", "-Werror", "-Wno-unused-function"]
    if args.sanitize:
        command += ["-fsanitize=address,undefined", "-fno-omit-frame-pointer", "-g"]
    for include in [Path(temporary), here / "stubs", root / "lib/Serialization", root / "lib/Epub/Epub",
                    root / "lib/Epub/Epub/blocks", root / "lib/Epub/Epub/converters"]:
        command += ["-I", str(include)]
    command += [str(source), str(here / "image_cache_test.cpp"), "-o", str(binary)]
    subprocess.run(command, check=True, timeout=60)
    subprocess.run([str(binary)], check=True, timeout=30)
