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

static uint32_t ticks = 0, tickStep = 0;
static unsigned yields = 0;
uint32_t millis() { const auto now = ticks; ticks += tickStep; return now; }
void vTaskDelay(int count) { assert(count == 1); ++yields; }
// Storage/extraction and cache persistence are test boundaries; production
// caller bodies, Expat parsers, fallback dispatch and normalizer run unchanged.
struct FsFile {
  std::string data, path;
  size_t pos = 0, readLimit = SIZE_MAX, chunkLimit = SIZE_MAX;
  int terminalRead = 0;
  bool failClose = false, opened = false;
  static unsigned live;
  size_t size() const { return data.size(); }
  bool available() const { return pos < data.size() && pos < readLimit; }
  int read(uint8_t* dst, size_t count) {
    if (pos >= readLimit) return terminalRead;
    count = std::min({count, data.size() - pos, readLimit - pos, chunkLimit});
    std::memcpy(dst, data.data() + pos, count);
    pos += count;
    return static_cast<int>(count);
  }
  bool close() { if (opened) { opened = false; --live; } return !failClose; }
  ~FsFile() { close(); }
};
unsigned FsFile::live = 0;
struct StorageMock {
  bool failWrite = false, failRead = false, failWriteClose = false, failReadClose = false;
  std::string faultPath;
  size_t readLimit = SIZE_MAX, chunkLimit = SIZE_MAX;
  int terminalRead = 0;
  unsigned removed = 0;
  bool applies(const std::string& path) const { return faultPath.empty() || path == faultPath; }
  bool openFileForWrite(const char*, const std::string& path, FsFile& file) {
    if (failWrite && applies(path)) return false;
    file.path = path; file.opened = true; ++FsFile::live;
    file.failClose = failWriteClose && applies(path);
    return true;
  }
  bool openFileForRead(const char*, const std::string& path, FsFile& file) {
    if (failRead && applies(path)) return false;
    file.pos = 0; file.opened = true; ++FsFile::live;
    file.failClose = failReadClose && applies(path);
    if (applies(path)) {
      file.readLimit = readLimit; file.chunkLimit = chunkLimit; file.terminalRead = terminalRead;
    }
    return true;
  }
  void remove(const char*) { assert(FsFile::live == 0); ++removed; }
} Storage;
class Epub {
 public:
  std::string contentBasePath = "OPS/", tocNcxItem, tocNavItem;
  std::map<std::string, std::string> archive;
  std::string failedExtraction;
  mutable unsigned extractions = 0;
  std::unique_ptr<BookMetadataCache> bookMetadataCache = std::make_unique<BookMetadataCache>();
  std::string getCachePath() const { return "cache"; }
  bool readItemContentsToStream(const std::string& path, FsFile& file, size_t) const {
    ++extractions;
    auto it = archive.find(path);
    if (it == archive.end()) return false;
    file.data = it->second;
    return path != failedExtraction;
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
  assert(Storage.removed >= 8);
  assert(FsFile::live == 0);
  // Missing, empty, corrupt and partial nav output must use a clean NCX fallback.
  for (int fault = 0; fault < 11; ++fault) {
    Storage = {};
    Epub fallback;
    fallback.tocNavItem = "OPS/nav.xhtml";
    fallback.tocNcxItem = "OPS/toc.ncx";
    fallback.archive[fallback.tocNavItem] = nav("chapter.xhtml#nav");
    fallback.archive[fallback.tocNcxItem] = ncx("chapter.xhtml#ncx");
    Storage.faultPath = "cache/toc.nav";
    if (fault == 0) fallback.archive.erase(fallback.tocNavItem);
    if (fault == 1) fallback.archive[fallback.tocNavItem].clear();
    if (fault == 2) fallback.failedExtraction = fallback.tocNavItem; // Even valid XML is not success.
    if (fault == 3) fallback.archive[fallback.tocNavItem] += "<broken>"; // Already emitted entry.
    if (fault == 4) { Storage.readLimit = 100; Storage.terminalRead = 0; }
    if (fault == 5) { Storage.readLimit = 100; Storage.terminalRead = -1; }
    if (fault == 6) Storage.failWriteClose = true;
    if (fault == 7) Storage.failReadClose = true;
    if (fault == 8) Storage.failRead = true;
    if (fault == 9) Storage.failWrite = true;
    if (fault == 10) fallback.archive[fallback.tocNavItem].pop_back();
    assert(fallback.parseToc());
    expect(fallback, "OPS/chapter.xhtml", "ncx");
    assert(FsFile::live == 0);
    assert(Storage.removed == 2);
  }
  // Both failed formats discard any emitted prefix; a clean retry remains possible.
  Storage = {};
  book.tocNavItem = "OPS/nav.xhtml";
  book.archive[book.tocNavItem] = nav("chapter.xhtml#partial") + "<broken>";
  book.archive[book.tocNcxItem] = ncx("../Text/ch1.xhtml#partial") + "<broken>";
  book.bookMetadataCache->entries.clear();
  assert(!book.parseToc());
  assert(book.bookMetadataCache->entries.empty());
  assert(FsFile::live == 0);
  book.tocNavItem.clear();
  book.failedExtraction = book.tocNcxItem;
  book.archive[book.tocNcxItem] = ncx("../Text/ch1.xhtml#retry");
  assert(!book.parseToc());
  assert(book.bookMetadataCache->entries.empty());
  book.failedExtraction.clear();
  Storage.chunkLimit = 1; // Every parser callback and finalization boundary.
  assert(book.parseToc());
  expect(book, "OPS/Text/ch1.xhtml", "retry");
  book.bookMetadataCache->entries.clear();
  Storage = {};
  book.archive[book.tocNcxItem] += "<broken>";
  book.bookMetadataCache->failReset = true;
  assert(!book.parseToc()); // Refuse publication when cache rollback itself fails.
  assert(FsFile::live == 0);
  book.bookMetadataCache->failReset = false;
  book.bookMetadataCache->entries.clear();
  book.archive[book.tocNcxItem] = ncx("../Text/ch1.xhtml#retry");
  ticks = UINT32_MAX - 40; tickStep = 10; yields = 0;
  Storage.chunkLimit = 1;
  assert(book.parseToc()); // elapsed subtraction remains valid through tick rollover.
  assert(yields > 0);
  expect(book, "OPS/Text/ch1.xhtml", "retry");
  book.bookMetadataCache->entries.clear();
  ticks = UINT32_MAX - 40; tickStep = 30000;
  assert(!book.parseToc()); // Deadline failure cleans up and discards partial entries.
  assert(book.bookMetadataCache->entries.empty() && FsFile::live == 0);
  ticks = 0; tickStep = 0; yields = 0;
  Storage.chunkLimit = SIZE_MAX;
  book.archive[book.tocNcxItem] = "<ncx>" + std::string(20000, ' ') + "</ncx>";
  assert(book.parseToc()); // Byte checkpoint still yields when modeled time is stationary.
  assert(yields > 0 && FsFile::live == 0);
  std::puts("PASS: NCX path resolution, fragments, EPUB3 preference/fallback and error/retry");
}
