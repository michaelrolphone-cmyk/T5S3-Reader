#include "ContainerParser.h"
#include "fixtures.h"
#include <cstdlib>
#include <iostream>

static void require(bool ok, const std::string& what) {
  if (!ok) { std::cerr << "FAIL: " << what << '\n'; std::exit(1); }
}
static std::string rootfile(const std::string& path, const char* media = "application/oebps-package+xml") {
  return "<rootfile full-path=\"" + path + "\" media-type=\"" + media + "\"/>";
}
static std::string document(const std::string& entries) {
  return "<?xml version=\"1.0\"?><container version=\"1.0\" "
         "xmlns=\"urn:oasis:names:tc:opendocument:xmlns:container\"><rootfiles>" +
         entries + "</rootfiles></container>";
}
static void check(const std::string& name, const std::string& entries, const std::string& expected, size_t chunk) {
  const std::string xml = document(entries);
  {
    ContainerParser parser(xml.size());
    require(parser.setup(), name + " setup");
    for (size_t pos = 0; pos < xml.size();) {
      const size_t count = std::min(chunk, xml.size() - pos);
      const size_t written = chunk == 1 ? parser.write(static_cast<uint8_t>(xml[pos])) :
          parser.write(reinterpret_cast<const uint8_t*>(xml.data() + pos), count);
      require(written == count, name + " parse");
      pos += count;
    }
    require(parser.fullPath == expected, name + " expected " + expected + " got " + parser.fullPath);
  }
  Epub epub;
  epub.xml = xml;
  epub.chunk = chunk;
  std::string path = "unchanged";
  require(epub.findContentOpfFile(&path) == !expected.empty(), name + " caller result");
  require(path == (expected.empty() ? "unchanged" : expected), name + " caller selection");
}
int main() {
  const auto first = rootfile("Preferred/package.opf"), second = rootfile("Secondary/package.opf");
  for (size_t chunk : {size_t(1), size_t(7), size_t(512), size_t(4096)}) {
    check("default rendition", first + second, "Preferred/package.opf", chunk);
    check("reversed rendition order", second + first, "Secondary/package.opf", chunk);
    check("one rendition", first, "Preferred/package.opf", chunk);
    check("duplicate rendition", first + first, "Preferred/package.opf", chunk);
    check("unsupported before supported", rootfile("other.xml", "application/xml") + first + second,
          "Preferred/package.opf", chunk);
    check("missing attributes", "<rootfile/><rootfile full-path=\"missing-type.opf\"/>"
          "<rootfile media-type=\"application/oebps-package+xml\"/>" + first, "Preferred/package.opf", chunk);
    check("empty path skipped", rootfile("") + first, "Preferred/package.opf", chunk);
    check("empty later path ignored", first + rootfile(""), "Preferred/package.opf", chunk);
    check("no rootfiles", "", "", chunk);
    check("only unsupported", rootfile("other.xml", "application/xml"), "", chunk);
    check("XML character references", rootfile("A&amp;B/package.opf") + second, "A&B/package.opf", chunk);
    check("UTF-8 path", rootfile(u8"Édition/内容.opf") + second, u8"Édition/内容.opf", chunk);
    check("internal and external chunks", first + "<!--" + std::string(2200, 'x') + "-->" + second,
          "Preferred/package.opf", chunk);
  }
  {
    ContainerParser parser(1);
    require(parser.write(uint8_t('x')) == 0, "uninitialized parser refuses input");
  }
  for (const auto& invalid : {
      document(first) + "<extra/>",
      std::string("<container><rootfiles>") + first + "</container>",
      std::string("<container><rootfiles>") + first,
      document(first + "<broken>")}) {
    ContainerParser parser(invalid.size());
    require(parser.setup(), "invalid setup");
    require(parser.write(reinterpret_cast<const uint8_t*>(invalid.data()), invalid.size()) == 0,
            "malformed/truncated document rejected after selected rootfile");
    require(parser.write(uint8_t('x')) == 0, "failed parser refuses later bytes");
    Epub epub;
    epub.xml = invalid;
    std::string path = "unchanged";
    require(!epub.findContentOpfFile(&path) && path == "unchanged", "caller never publishes partial selection");
  }
  Epub epub;
  epub.xml = document(first + second);
  std::string path = "unchanged";
  epub.sizeOk = false;
  require(!epub.findContentOpfFile(&path) && path == "unchanged", "size failure preserves output");
  epub.sizeOk = true;
  epub.streamOk = false;
  require(!epub.findContentOpfFile(&path) && path == "unchanged", "stream failure preserves output");
  epub.streamOk = true;
  epub.chunk = 7;
  epub.failAfter = epub.xml.find("<rootfile", epub.xml.find("<rootfile ") + 1);
  require(!epub.findContentOpfFile(&path) && path == "unchanged" && epub.bytesWritten > 0,
          "mid-stream failure after selection preserves output");
  epub.failAfter = std::numeric_limits<size_t>::max();
  require(epub.findContentOpfFile(&path) && path == "Preferred/package.opf", "fresh retry succeeds");
  for (int repeat = 0; repeat < 20; ++repeat)
    check("repeated fresh parser and cleanup", first + second, "Preferred/package.opf", 3);
  // Upgrade from the old rendition policy: reusable content must not address
  // any legacy derived artifact, even when embedded styles are disabled.
  Storage.files = {"/cache/css_rules.cache", "/cache/sections/0.bin", "/cache/img_0_0.pxc",
      "/cache/cover.bmp", "/cache/cover_crop.bmp", "/cache/thumb_240.bmp",
      "/cache/progress.bin", "/cache/unknown-user-file"};
  const auto legacyFiles = Storage.files;
  CssParser css;
  require(!css.hasCache(), "legacy rendition stylesheet ignored");
  for (const auto& derived : {sectionPath(&epub, 0), imagePrefix(&epub, 0) + "0.pxc",
      epub.getCoverBmpPath(false), epub.getCoverBmpPath(true), epub.getThumbBmpPath(240)})
    require(!Storage.exists(derived.c_str()), "legacy rendition artifact ignored: " + derived);
  require(sectionPath(&epub, 0) == sectionDirectory(&epub) + "/0.bin", "section create/load paths match");
  auto thumb = epub.getThumbBmpPath();
  const auto placeholder = thumb.find("[HEIGHT]");
  require(placeholder != std::string::npos, "thumbnail placeholder retained");
  thumb.replace(placeholder, 8, "240");
  require(thumb == epub.getThumbBmpPath(240), "thumbnail template matches sized path");
  require(epub.getCachePath() == "/cache" && Storage.files == legacyFiles,
          "stable progress root and all legacy/unknown files preserved");
  Storage.files.insert("/cache/rendition_v1_css_rules.cache");
  require(css.hasCache(), "fresh-generation stylesheet reused normally");
  std::cout << "52 rendition/chunk cases, malformed/truncated XML, caller failures, retry and cleanup passed\n";
}
