#include "ContentOpfParser.h"
#include "fixtures.h"
#include <cstdlib>
#include <iostream>

static void require(bool ok, const std::string& what) {
  if (!ok) { std::cerr << "FAIL: " << what << '\n'; std::exit(1); }
}
static std::string reference(const char* type, const char* href) {
  return std::string("<reference type=\"") + type + "\" href=\"" + href + "\"/>";
}
static std::string document(const std::string& guide, bool prefixed = false) {
  const std::string p = prefixed ? "opf:" : "";
  return "<" + p + "package xmlns:opf=\"http://www.idpf.org/2007/opf\">"
         "<" + p + "manifest><" + p + "item id=\"cover\" href=\"cover.xhtml\"/>"
         "<" + p + "item id=\"body\" href=\"body.xhtml\"/></" + p + "manifest>"
         "<" + p + "spine><" + p + "itemref idref=\"cover\"/><" + p + "itemref idref=\"body\"/>"
         "</" + p + "spine><" + p + "guide>" + guide + "</" + p + "guide></" + p + "package>";
}
static void run(const std::string& name, const std::string& guide, const std::string& expected,
                size_t chunk, bool prefixed = false) {
  const std::string xml = document(guide, prefixed), cachePath = "/cache", basePath = "OPS/";
  BookMetadataCache cache;
  {
    ContentOpfParser parser(cachePath, basePath, xml.size(), &cache);
    require(parser.setup(), name + " setup");
    for (size_t offset = 0; offset < xml.size();) {
      const size_t count = std::min(chunk, xml.size() - offset);
      require(parser.write(reinterpret_cast<const uint8_t*>(xml.data() + offset), count) == count, name + " parse");
      offset += count;
    }
    require(parser.textReferenceHref == expected, name + " expected " + expected + " got " + parser.textReferenceHref);
    if (name == "cover unchanged") require(parser.guideCoverPageHref == "OPS/cover.xhtml", "cover target retained");
    require(cache.spine == std::vector<std::string>{"OPS/cover.xhtml", "OPS/body.xhtml"}, name + " manifest/spine preserved");
  }
  require(Storage.files.empty(), name + " temporary cache cleaned");
}

static void runAuthors(const std::string& name, const std::string& creators, const std::string& expected,
                       size_t chunk, size_t padding = 0) {
  const std::string xml = "<package><metadata><dc:title>Title</dc:title>" + std::string(padding, ' ') + creators +
                          "<dc:language>en</dc:language></metadata><manifest><item id=\"body\" "
                          "href=\"body.xhtml\"/></manifest><spine><itemref idref=\"body\"/></spine></package>";
  const std::string cachePath = "/cache", basePath = "OPS/";
  BookMetadataCache cache;
  {
    ContentOpfParser parser(cachePath, basePath, xml.size(), &cache);
    require(parser.setup(), name + " setup");
    for (size_t offset = 0; offset < xml.size();) {
      const size_t count = std::min(chunk, xml.size() - offset);
      require(parser.write(reinterpret_cast<const uint8_t*>(xml.data() + offset), count) == count, name + " parse");
      offset += count;
    }
    require(parser.author == expected, name + " expected " + expected + " got " + parser.author);
    require(parser.title == "Title" && parser.language == "en", name + " neighboring metadata preserved");
    require(cache.spine == std::vector<std::string>{"OPS/body.xhtml"}, name + " manifest/spine preserved");
  }
  require(Storage.files.empty(), name + " temporary cache cleaned");
}

int main() {
  for (size_t chunk : {size_t(1), size_t(2), size_t(7), size_t(1023), size_t(1024), size_t(4096)}) {
    runAuthors("single creator", "<dc:creator>Jane Doe</dc:creator>", "Jane Doe", chunk);
    runAuthors("multiple creators", "<dc:creator>Jane Doe</dc:creator><dc:creator>John Smith</dc:creator>",
               "Jane Doe, John Smith", chunk);
    runAuthors("entities and UTF-8", "<dc:creator>José &amp; Zoë &#x1F642;</dc:creator>", "José & Zoë \U0001F642", chunk);
    runAuthors("CDATA and comment boundaries", "<dc:creator>Jane<![CDATA[ & ]]><!-- split -->Doe</dc:creator>",
               "Jane & Doe", chunk);
    runAuthors("empty creators", "<dc:creator/><dc:creator>Jane</dc:creator><dc:creator></dc:creator>"
               "<dc:creator>Doe</dc:creator><dc:creator/>", "Jane, Doe", chunk);
    runAuthors("all creators empty", "<dc:creator/><dc:creator></dc:creator>", "", chunk);
    runAuthors("no creators", "", "", chunk);
    runAuthors("preserve creator whitespace", "<dc:creator> Jane  Doe </dc:creator>", " Jane  Doe ", chunk);
    // Place creator text across the production write() internal 1024-byte buffer boundary.
    runAuthors("internal buffer boundary", "<dc:creator>Jane Doe</dc:creator>", "Jane Doe", chunk, 963);
  }
  const std::string cachePath = "/cache", basePath = "OPS/";
  {
    const std::string prefix = "<package><manifest/><metadata><dc:creator>First</dc:creator><dc:creator>Partial";
    const std::string suffix = "</wrong></metadata></package>";
    ContentOpfParser parser(cachePath, basePath, prefix.size() + suffix.size(), nullptr);
    require(parser.setup(), "incomplete creator setup");
    require(parser.write(reinterpret_cast<const uint8_t*>(prefix.data()), prefix.size()) == prefix.size(),
            "incomplete creator prefix parsed");
    require(parser.write(reinterpret_cast<const uint8_t*>(suffix.data()), suffix.size()) == 0,
            "malformed creator rejected");
    const std::string rejectedAuthor = parser.author;
    require(parser.write(uint8_t('x')) == 0, "failed creator parser refuses retry bytes");
    require(parser.author == rejectedAuthor, "failed parser leaves author unchanged on retry");
  }
  require(Storage.files.empty(), "malformed creator temporary cache cleaned");
  for (const std::string& invalid : {
           std::string("<package><manifest/><metadata><dc:creator>Truncated"),
           std::string("<package><manifest/><metadata><dc:creator>Bad\xC3\x28</dc:creator></metadata></package>")}) {
    ContentOpfParser parser(cachePath, basePath, invalid.size(), nullptr);
    require(parser.setup(), "invalid author setup");
    require(parser.write(reinterpret_cast<const uint8_t*>(invalid.data()), invalid.size()) == 0,
            "truncated or invalid UTF-8 author rejected");
    require(parser.write(uint8_t('x')) == 0, "invalid author refuses retry bytes");
  }
  require(Storage.files.empty(), "invalid author temporary cache cleaned");
  for (int i = 0; i < 3; ++i)
    runAuthors("fresh author retry", "<dc:creator>Fresh Author</dc:creator>", "Fresh Author", 1);
  const auto text = reference("text", "body.xhtml"), start = reference("start", "fallback.xhtml");
  for (size_t chunk : {size_t(1), size_t(7), size_t(4096)}) {
    run("start-only fallback", start, "OPS/fallback.xhtml", chunk);
    run("text-only", text, "OPS/body.xhtml", chunk);
    run("start then text", start + text, "OPS/body.xhtml", chunk);
    run("text then start", text + start, "OPS/body.xhtml", chunk);
    run("first fallback retained", start + reference("start", "later.xhtml"), "OPS/fallback.xhtml", chunk);
    run("no guide references", "", "", chunk);
    run("unrelated guide", reference("toc", "toc.xhtml"), "", chunk);
    run("missing href", "<reference type=\"start\"/>", "", chunk);
    run("cover unchanged", reference("cover", "cover.xhtml") + text, "OPS/body.xhtml", chunk);
    run("prefixed guide", "<opf:reference type=\"start\" href=\"body.xhtml\"/>", "OPS/body.xhtml", chunk, true);
  }
  const std::string broken = "<package><manifest><item id=\"x\" href=\"x\"/></manifest><guide><broken></guide></package>";
  {
    ContentOpfParser parser(cachePath, basePath, broken.size(), nullptr);
    require(parser.setup(), "malformed setup");
    require(parser.write(reinterpret_cast<const uint8_t*>(broken.data()), broken.size()) == 0, "malformed XML rejected");
    require(parser.write(uint8_t('x')) == 0, "failed parser refuses retry bytes");
  }
  require(Storage.files.empty(), "malformed parser temporary cache cleaned");
  run("fresh retry after parse failure", text + start, "OPS/body.xhtml", 3);
  for (int i = 0; i < 3; ++i) run("repeat open", start, "OPS/fallback.xhtml", 7);
  for (uint8_t version : {5, 6, 7, 8, 9, 10}) {
    FsFile file;
    Storage.openFileForWrite("fixture", "/cache/book.bin", file);
    serialization::writePod(file, version);
    serialization::writePod(file, uint32_t(0));
    serialization::writePod(file, uint16_t(2));
    serialization::writePod(file, uint16_t(0));
    for (const char* field : {"Title", "Author", "en", "OPS/cover.xhtml", "OPS/body.xhtml"})
      serialization::writeString(file, field);
    BookMetadataCache cache;
    require(cache.load() == (version == 10), "cache generation acceptance");
    require(cache.loaded == (version == 10), "old cache not published");
    if (version != 10) require(!cache.bookFile, "old cache handle closed");
    else {
      require(cache.coreMetadata.textReferenceHref == "OPS/body.xhtml", "current target restored");
      require(cache.coreMetadata.author == "Author", "current author restored");
    }
    Storage.files.clear();
  }
  std::cout << "57 author chunk cases, 34 guide cases, malformed XML, retry, cleanup and cache generations 5-10 passed\n";
}
