#include <HalStorage.h>
#include <Logging.h>
#include <FsHelpers.h>
#include <ZipFile.h>
#include <ProgressMapper.h>
#include <BookmarkUtil.h>
#include <algorithm>
#include <optional>
#include <fstream>
#include <iterator>
#include <cstdio>
#include <iomanip>
#include <sstream>

Counts counts;
uint32_t clockMs = 0, readMs = 0;
std::string archivePath, fault;
int liveArchives = 0, liveTemps = 0, failReadCountdown = 0, readFailure = 0, failSeekCountdown = 0;
StorageFixture Storage;
unsigned long long deliveredBytes = 0, deliveryCalls = 0;
unsigned mkdirCalls = 0;
bool mkdirResult = true;
uint32_t millis() { return clockMs; }
void vTaskDelay(int) { ++counts.yields; ++clockMs; }
namespace FsHelpers {
#include "normalise.inc"
}
#include "epub.inc"
struct BookmarkEntry {
  float percentage = 0;
  std::string xpath, summary;
  uint16_t computedSpineIndex = 0, computedChapterPageCount = 0, computedChapterProgress = 0;
};
constexpr float bookmarkProgressEpsilon = 0.0001f;
#include "range.inc"
struct Page {};
unsigned paragraphLookups = 0, pageLoads = 0;
int lastParagraphPage = -1;
struct Section {
  int currentPage = 10, pageCount = 100;
  bool hasIndex = true, loadSuccess = true;
  uint16_t paragraphIndex = 1;
  std::optional<uint16_t> getParagraphIndexForPage(uint16_t page) const {
    ++paragraphLookups;
    lastParagraphPage = page;
    return hasIndex ? std::optional<uint16_t>(paragraphIndex) : std::nullopt;
  }
  std::shared_ptr<Page> loadPageFromSectionFile() {
    ++pageLoads;
    return loadSuccess ? std::make_shared<Page>() : nullptr;
  }
};
std::string extractPageText(const Page&) { return "  Summary\n  multilingual café 日本語 " + std::string(90, 'a'); }
unsigned saves = 0;
bool saveSuccess = true;
std::vector<BookmarkEntry> persisted;
namespace JsonSettingsIO {
bool saveBookmarks(const std::vector<BookmarkEntry>& b, const char* path) {
  ++saves;
  assert(std::string(path) == BookmarkUtil::getBookmarkPath(archivePath));
  if (saveSuccess) persisted = b;
  return saveSuccess;
}
}
class EpubReaderActivity {
 public:
  std::shared_ptr<Epub> epub;
  std::shared_ptr<Section> section;
  int currentSpineIndex = 0;
  std::vector<BookmarkEntry> cachedBookmarks;
  bool bookmarkRemoved = false;
  void addBookmark();
};
unsigned locks = 0, unlocks = 0;
struct RenderLock {
  explicit RenderLock(EpubReaderActivity&) { ++locks; }
  ~RenderLock() { ++unlocks; }
};
#include "bookmark.inc"

std::string hex(const std::string& text) {
  std::ostringstream out;
  for (unsigned char c : text) out << std::hex << std::setw(2) << std::setfill('0') << unsigned(c);
  return out.str();
}
std::string snapshot(const std::vector<BookmarkEntry>& bookmarks) {
  std::ostringstream out;
  for (const auto& b : bookmarks) {
    out << std::hexfloat << b.percentage << ':' << hex(b.xpath) << ':' << hex(b.summary) << ':'
        << b.computedSpineIndex << ':' << b.computedChapterPageCount << ':' << b.computedChapterProgress << ';';
  }
  return out.str();
}
BookmarkEntry identity(const EpubReaderActivity& a) {
  BookmarkEntry b;
  b.percentage = 0.9f;  // Intentionally outside the current range: cached identity wins.
  b.xpath = "existing/xpath";
  b.summary = "existing summary";
  b.computedSpineIndex = a.currentSpineIndex;
  b.computedChapterPageCount = a.section->pageCount;
  b.computedChapterProgress = a.section->currentPage;
  return b;
}
void action(EpubReaderActivity& a, const std::string& name, int expected, bool healthy = false) {
  const bool active = a.epub && a.section;
  const auto beforeSaves = saves, beforeLocks = locks, beforeUnlocks = unlocks;
  counts = {};
  deliveredBytes = deliveryCalls = paragraphLookups = pageLoads = mkdirCalls = 0;
  lastParagraphPage = -1;
  const auto durableBefore = snapshot(persisted);
  a.addBookmark();
  assert(liveArchives == 0 && liveTemps == 0 && counts.opens == counts.closes);
  assert(saves == beforeSaves + unsigned(active));
  assert(locks == beforeLocks + unsigned(active) && unlocks == beforeUnlocks + unsigned(active));
  assert(mkdirCalls == (active ? 2u : 0u));
  if (expected >= 0) assert(a.bookmarkRemoved == bool(expected));
  if (active && saveSuccess) assert(snapshot(a.cachedBookmarks) == snapshot(persisted));
  else assert(durableBefore == snapshot(persisted));
  if (!active) assert(counts.opens == 0 && paragraphLookups == 0 && pageLoads == 0);
  if (active && a.bookmarkRemoved) {
    assert(pageLoads == 0);
#ifndef BASELINE_SOURCE
    assert(counts.opens == 0 && counts.reads == 0 && deliveredBytes == 0 && paragraphLookups == 0);
#endif
  }
  if (active && !a.bookmarkRemoved) {
    const bool valid = a.section->currentPage >= 0 && a.section->currentPage < a.section->pageCount;
    assert(paragraphLookups == unsigned(valid) && pageLoads == unsigned(valid));
    if (valid) assert(lastParagraphPage == (a.section->currentPage > 0 ? a.section->currentPage - 1 : 0));
    if (healthy) {
      assert(!a.cachedBookmarks.front().xpath.empty());
      const auto passes = a.section->hasIndex ? 1u : 2u;
      assert(deliveredBytes == a.epub->chapterSize * passes);
      assert(counts.yields > 0);
    }
  }
  printf("{\"case\":\"%s\",\"removed\":%d,\"cached\":\"%s\",\"durable\":\"%s\",\"saves\":%u,\"mkdirs\":%u,\"page_loads\":%u,\"output_bytes\":%llu,\"output_calls\":%llu,\"opens\":%llu,\"reads\":%llu,\"yields\":%llu,\"paragraph_lookups\":%u}\n",
         name.c_str(), a.bookmarkRemoved, snapshot(a.cachedBookmarks).c_str(), snapshot(persisted).c_str(), saves - beforeSaves,
         mkdirCalls, pageLoads, deliveredBytes, deliveryCalls, (unsigned long long)counts.opens,
         (unsigned long long)counts.reads, (unsigned long long)counts.yields, paragraphLookups);
}
int main(int argc, char** argv) {
  assert(argc == 5);
  archivePath = "/Books/book.epub";
  auto data = std::make_shared<MemFile>();
  std::ifstream in(argv[1], std::ios::binary);
  assert(in);
  data->bytes = std::vector<uint8_t>(std::istreambuf_iterator<char>(in), {});
  Storage.files[archivePath] = data;
  EpubReaderActivity a;
  a.epub = std::make_shared<Epub>();
  a.epub->filepath = archivePath;
  a.epub->chapterSize = std::stoull(argv[2]);
  a.section = std::make_shared<Section>();
  a.section->hasIndex = std::stoi(argv[3]);
  a.section->currentPage = a.section->hasIndex ? 0 : 10;
  action(a, "addition", 0, std::string(argv[4]) != "malformed");
  action(a, "immediate-removal", 1);
  a.cachedBookmarks = {identity(a)};
  action(a, "seeded-removal", 1);
  if (std::string(argv[4]) != "full") return 0;

  auto other = identity(a);
  other.computedSpineIndex = 7;
  other.percentage = 0.8f;
  other.summary = "keep first";
  auto other2 = other;
  other2.summary = "keep second";
  a.cachedBookmarks = {other, identity(a), other2, identity(a)};
  action(a, "duplicate-removal-stable-order", 1);
  assert(a.cachedBookmarks.size() == 2 && a.cachedBookmarks[0].summary == "keep first" && a.cachedBookmarks[1].summary == "keep second");
  action(a, "nonmatch-addition", 0, true);
  assert(a.cachedBookmarks.size() == 3 && a.cachedBookmarks[1].summary == "keep first");
  const auto range = getPageProgressRange(a.epub, 0, a.section->currentPage, a.section->pageCount);
  for (auto p : {range.start, range.end, range.start - bookmarkProgressEpsilon / 2, range.end + bookmarkProgressEpsilon / 2}) {
    other.percentage = p;
    a.cachedBookmarks = {other};
    action(a, "percentage-boundary-removal", 1);
    assert(a.cachedBookmarks.empty());
  }
  other.percentage = range.end + 2 * bookmarkProgressEpsilon;
  a.cachedBookmarks = {other};
  action(a, "percentage-outside-addition", 0, true);
  assert(a.cachedBookmarks.size() == 2);

  // Preserve the existing BUG81 mutate-before-save semantics, including retry.
  a.cachedBookmarks = {identity(a)};
  persisted = a.cachedBookmarks;
  saveSuccess = false;
  action(a, "remove-save-failure", 1);
  assert(a.cachedBookmarks.empty() && persisted.size() == 1);
  saveSuccess = true;
  action(a, "toggle-after-save-failure", 0, true);
  saveSuccess = false;
  a.cachedBookmarks.clear();
  action(a, "add-save-failure", 0, true);
  saveSuccess = true;
  action(a, "remove-after-add-save-failure", 1);

  mkdirResult = false;
  a.cachedBookmarks.clear();
  action(a, "mkdir-failure-addition", 0, true);
  action(a, "mkdir-failure-removal", 1);
  mkdirResult = true;
  a.section->loadSuccess = false;
  action(a, "page-load-failure", 0, true);
  assert(a.cachedBookmarks.front().summary.empty());
  action(a, "page-load-failure-removal", 1);
  a.section->loadSuccess = true;

  for (const char* mode : {"open", "header", "metadata", "seek"}) {
    fault = mode;
    a.cachedBookmarks.clear();
    action(a, std::string("archive-fault-add-") + mode, 0);
    fault.clear();
    action(a, "archive-fault-following-removal", 1);
    action(a, "archive-fault-retry-add", 0, true);
    action(a, "archive-fault-retry-remove", 1);
    a.cachedBookmarks = {identity(a)};
    fault = mode;
    action(a, std::string("archive-fault-remove-") + mode, 1);
    fault.clear();
  }
  for (int failure : {0, -1}) {
    failReadCountdown = 12;
    readFailure = failure;
    a.cachedBookmarks.clear();
    action(a, "read-fault-addition", 0);
    failReadCountdown = 0;
    action(a, "read-fault-removal", 1);
    action(a, "read-fault-retry", 0, true);
    action(a, "read-fault-retry-removal", 1);
  }
  const int originalPage = a.section->currentPage;
  for (const auto& pageAndCount : {std::pair<int, int>{-1,100}, {100,100}, {0,0}, {0,1}, {99,100}}) {
    a.section->currentPage = pageAndCount.first;
    a.section->pageCount = pageAndCount.second;
    a.cachedBookmarks.clear();
    action(a, "edge-page-addition", 0);
    action(a, "edge-page-removal", 1);
  }
  a.section->currentPage = originalPage;
  a.section->pageCount = 100;
  a.section->paragraphIndex = 0;
  action(a, "zero-paragraph-addition", 0);
  action(a, "zero-paragraph-removal", 1);
  a.section->paragraphIndex = 1;
  a.epub->chapterSize = 0;
  action(a, "empty-metadata-addition", 0);
  action(a, "empty-metadata-removal", 1);
  a.epub->chapterSize = std::stoull(argv[2]);
  auto section = a.section;
  a.section.reset();
  action(a, "no-section", -1);
  a.section = section;
  a.epub.reset();
  action(a, "no-epub", -1);
}
