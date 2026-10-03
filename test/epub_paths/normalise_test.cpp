#include "ContentOpfParser.h"
#include "FsHelpers.h"
#include "fixtures.h"
#include <cstdlib>
#include <iostream>
#include <utility>

static void require(bool ok, const std::string& what) {
  if (!ok) { std::cerr << "FAIL: " << what << '\n'; std::exit(1); }
}

static void pathCases() {
  const std::vector<std::pair<std::string, std::string>> cases = {
      {"OPS/./chapter.xhtml", "OPS/chapter.xhtml"},
      {"./chapter.xhtml", "chapter.xhtml"},
      {"././OPS/./Text/./chapter.xhtml", "OPS/Text/chapter.xhtml"},
      {"OPS/.", "OPS"}, {"OPS/./", "OPS"}, {".", ""}, {"./", ""},
      {"././.", ""}, {"", ""}, {"/", ""}, {"///./", ""},
      {"OPS//Text///chapter.xhtml", "OPS/Text/chapter.xhtml"},
      {"/OPS/./chapter.xhtml", "OPS/chapter.xhtml"},
      {"a/b/../c", "a/c"}, {"a/./b/../c", "a/c"},
      {"./a/b/../../c", "c"}, {"../chapter.xhtml", "chapter.xhtml"},
      {"OPS/.hidden.xhtml", "OPS/.hidden.xhtml"},
      {"OPS/.../chapter.xhtml", "OPS/.../chapter.xhtml"},
      {"OPS/chapter..xhtml", "OPS/chapter..xhtml"},
      {"OPS/./café/./漢字.xhtml", "OPS/café/漢字.xhtml"},
      {"OPS/file%20name.xhtml", "OPS/file%20name.xhtml"},
      // Preserve existing trailing-parent and archive-relative-root conventions.
      {"a/b/..", "a/b/.."}, {"a/b/../", "a"},
  };
  for (int repeat = 0; repeat < 3; ++repeat) {
    for (const auto& entry : cases) {
      const auto actual = FsHelpers::normalisePath(entry.first);
      require(actual == entry.second, "normalise " + entry.first + " expected " + entry.second + " got " + actual);
      require(FsHelpers::normalisePath(actual) == actual, "idempotent " + entry.first);
    }
  }
}

static std::string document(const std::string& prefix) {
  return "<package><manifest>"
         "<item id=\"chapter\" href=\"" + prefix + "chapter.xhtml\"/>"
         "<item id=\"style\" href=\"" + prefix + "style.css\" media-type=\"text/css\"/>"
         "<item id=\"ncx\" href=\"" + prefix + "toc.ncx\" media-type=\"application/x-dtbncx+xml\"/>"
         "<item id=\"nav\" href=\"" + prefix + "nav.xhtml\" properties=\"nav\"/>"
         "</manifest><spine><itemref idref=\"chapter\"/></spine>"
         "<guide><reference type=\"text\" href=\"" + prefix + "chapter.xhtml\"/></guide></package>";
}

static void parsePaths(const std::string& prefix, size_t chunk) {
  const std::string xml = document(prefix), cachePath = "/cache", basePath = "OPS/";
  BookMetadataCache cache;
  {
    ContentOpfParser parser(cachePath, basePath, xml.size(), &cache);
    require(parser.setup(), "parser setup");
    for (size_t offset = 0; offset < xml.size();) {
      const size_t count = std::min(chunk, xml.size() - offset);
      require(parser.write(reinterpret_cast<const uint8_t*>(xml.data() + offset), count) == count, "parse complete");
      offset += count;
    }
    require(cache.spine == std::vector<std::string>{"OPS/chapter.xhtml"}, "canonical chapter in production spine");
    require(parser.cssFiles == std::vector<std::string>{"OPS/style.css"}, "canonical CSS");
    require(parser.tocNcxPath == "OPS/toc.ncx", "canonical NCX");
    require(parser.tocNavPath == "OPS/nav.xhtml", "canonical nav");
    require(parser.textReferenceHref == "OPS/chapter.xhtml", "canonical guide");
    // Models the ZIP member-name equality at the read boundary, including old
    // cached dotted hrefs: production readers normalize again before lookup.
    require(FsHelpers::normalisePath("OPS/" + prefix + "chapter.xhtml") == cache.spine.front(),
            "cached href maps to the same existing archive member");
  }
  require(Storage.files.empty(), "temporary manifest store cleaned");
}

int main(int argc, char** argv) {
  const bool parserOnly = argc == 2 && std::string(argv[1]) == "--parser-only";
  if (!parserOnly) pathCases();
  for (size_t chunk : {size_t(1), size_t(7), size_t(4096)}) {
    for (const char* prefix : {"", "./", "././", "Text/.././"}) parsePaths(prefix, chunk);
  }
  const std::string broken = "<package><manifest><item id=\"x\" href=\"./x.xhtml\"/></broken>";
  const std::string cachePath = "/cache", basePath = "OPS/";
  {
    ContentOpfParser parser(cachePath, basePath, broken.size(), nullptr);
    require(parser.setup(), "malformed setup");
    require(parser.write(reinterpret_cast<const uint8_t*>(broken.data()), broken.size()) == 0, "malformed XML rejected");
    require(parser.write(uint8_t('x')) == 0, "failed parser refuses more input");
  }
  require(Storage.files.empty(), "failed parser cleanup");
  for (int retry = 0; retry < 3; ++retry) parsePaths("./", 3);
  std::cout << (parserOnly ? 0 : 72)
            << " path checks, 15 production OPF parses, malformed input, retry and cleanup passed\n";
}
