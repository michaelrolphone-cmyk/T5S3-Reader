#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <memory>
#include "Logging.h"
#include "TocNcxParser.h"
#include "TocNavParser.h"
#include "cache.h"

// Storage/extraction and cache persistence are test boundaries; production
// caller bodies, Expat parsers, fallback dispatch and normalizer run unchanged.
struct FsFile {
  std::string data;
  size_t pos = 0;
  size_t size() const { return data.size(); }
  bool available() const { return pos < data.size(); }
  size_t read(uint8_t* dst, size_t count) {
    count = std::min(count, data.size() - pos);
    std::memcpy(dst, data.data() + pos, count);
    pos += count;
    return count;
  }
  void close() {}
};
struct StorageMock {
  bool failWrite = false, failRead = false;
  unsigned removed = 0;
  bool openFileForWrite(const char*, const std::string&, FsFile&) { return !failWrite; }
  bool openFileForRead(const char*, const std::string&, FsFile&) { return !failRead; }
  void remove(const char*) { ++removed; }
} Storage;
class Epub {
 public:
  std::string contentBasePath = "OPS/", tocNcxItem, tocNavItem;
  std::map<std::string, std::string> archive;
  std::unique_ptr<BookMetadataCache> bookMetadataCache = std::make_unique<BookMetadataCache>();
  std::string getCachePath() const { return "cache"; }
  void readItemContentsToStream(const std::string& path, FsFile& file, size_t) const {
    file.data = archive.at(path);
  }
  bool parseTocNcxFile() const;
  bool parseTocNavFile() const;
  bool parseToc() const;
};
#include "callers.inc"
#include "cache_guard.inc"

std::string ncx(const std::string& href) {
  return "<ncx><navMap><navPoint><navLabel><text>Later chapter</text></navLabel><content src=\"" +
         href + "\"/></navPoint></navMap></ncx>";
}
std::string nav(const std::string& href) {
  return "<html><body><nav epub:type=\"toc\"><ol><li><a href=\"" + href +
         "\">Later chapter</a></li></ol></nav></body></html>";
}
void expect(const Epub& book, const std::string& href, const std::string& anchor) {
  const auto& entries = book.bookMetadataCache->entries;
  assert(entries.size() == 1);
  if (entries[0].href != href) {
    std::fprintf(stderr, "Expected %s, got %s\n", href.c_str(), entries[0].href.c_str());
    std::abort();
  }
  assert(entries[0].anchor == anchor);
  assert(entries[0].label == "Later chapter");
  // The emitted href must match the later spine entry, not default chapter 0.
  const std::vector<std::string> spine = {"OPS/Text/first.xhtml", href};
  assert(std::find(spine.begin(), spine.end(), entries[0].href) - spine.begin() == 1);
}
int main() {
  VersionFile oldCache{5};
  assert(!admitCachedToc(oldCache));
  assert(oldCache.closed);
  VersionFile newCache{BOOK_CACHE_VERSION};
  assert(admitCachedToc(newCache));
  assert(!newCache.closed);
  struct Case { const char* base; const char* toc; const char* src; const char* href; const char* anchor; };
  const Case cases[] = {
    {"OPS/", "OPS/Nav/toc.ncx", "../Text/ch1.xhtml#start", "OPS/Text/ch1.xhtml", "start"},
    {"OPS/", "OPS/toc.ncx", "Text/ch1.xhtml#start", "OPS/Text/ch1.xhtml", "start"},
    {"OPS/", "toc.ncx", "OPS/Text/ch1.xhtml#start", "OPS/Text/ch1.xhtml", "start"},
    {"OPS/", "Other/Deep/toc.ncx", "../../OPS/Text/ch1.xhtml", "OPS/Text/ch1.xhtml", ""},
    {"", "toc.ncx", "chapter.xhtml", "chapter.xhtml", ""},
  };
  for (const auto& c : cases) {
    Epub book;
    book.contentBasePath = c.base;
    book.tocNcxItem = c.toc;
    book.archive[c.toc] = ncx(c.src);
    assert(book.parseToc());
    expect(book, c.href, c.anchor);
  }
  Epub book;
  book.tocNcxItem = "OPS/Nav/toc.ncx";
  book.archive[book.tocNcxItem] = ncx("../Text/ch1.xhtml#ncx");
  book.tocNavItem = "OPS/Navigation/nav.xhtml";
  book.archive[book.tocNavItem] = nav("../Text/ch1.xhtml#nav");
  assert(book.parseToc());
  expect(book, "OPS/Text/ch1.xhtml", "nav");
  book.bookMetadataCache->entries.clear();
  book.archive[book.tocNavItem] = "<html><broken>";
  assert(book.parseToc());
  expect(book, "OPS/Text/ch1.xhtml", "ncx");
  book.bookMetadataCache->entries.clear();
  book.tocNavItem.clear();
  Storage.failWrite = true;
  assert(!book.parseToc());
  assert(book.bookMetadataCache->entries.empty());
  Storage.failWrite = false;
  Storage.failRead = true;
  assert(!book.parseToc());
  assert(book.bookMetadataCache->entries.empty());
  Storage.failRead = false;
  book.archive[book.tocNcxItem] = "<ncx><broken>";
  assert(!book.parseToc());
  assert(book.bookMetadataCache->entries.empty());
  book.archive[book.tocNcxItem] = ncx("../Text/ch1.xhtml#retry");
  assert(book.parseToc());
  expect(book, "OPS/Text/ch1.xhtml", "retry");
  Epub empty;
  assert(!empty.parseToc());
  assert(Storage.removed == 8);
  std::puts("PASS: NCX path resolution, fragments, EPUB3 preference/fallback and error/retry");
}
