#pragma once
#include <EpdFontFamily.h>
#include <SdCardFont.h>
#include <Markdown.h>
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <map>
#include <string>
#include <vector>

struct Work {
  size_t boundaries = 0, boundaryBytes = 0, boundaryTables = 0, boundaryGrowths = 0, maxBoundaries = 0;
  size_t measures = 0, measuredBytes = 0, boldMeasures = 0;
  size_t reads = 0, allocations = 0, releases = 0, yields = 0, prepared = 0;
};
inline Work work;
inline bool failAllocation = false;
inline void recordText(const std::string& value) {
  std::cout << value.size() << ':';
  std::cout.write(value.data(), value.size());
  std::cout << '\n';
}
inline void* testMalloc(size_t n) {
  ++work.allocations;
  std::cout << "allocate " << n << ' ' << failAllocation << '\n';
  if (failAllocation) return nullptr;
  return std::malloc(n);
}
inline void testFree(void* p) {
  std::cout << "free " << (p != nullptr) << '\n';
  if (p) ++work.releases;
  std::free(p);
}
inline void testDelay(unsigned n) {
  std::cout << "yield " << n << '\n';
  assert(n == 1); ++work.yields;
}
class GfxRenderer {
 public:
  std::map<int, EpdFontFamily> fontMap;
  std::map<int, SdCardFont*> sdCardFonts_;
  bool sd = false;
  bool isSdCardFont(int) const { return sd; }
  void ensureSdCardFontReady(int id, const char* s, uint8_t mask) const {
    ++work.prepared;
    std::cout << "prepare " << id << ' ' << unsigned(mask) << '\n';
    recordText(s);
  }
  int getTextAdvanceX(int, const char*, EpdFontFamily::Style) const;
  int getLineHeight(int) const;
};
struct Txt {
  // This oracle uses independent page reads. The real ReadWindow/SD/FatFs
  // path is covered by test/txt_index/index_test.py, including failure/retry.
  struct ReadWindow {
    static constexpr size_t CAPACITY = 8 * 1024;
    const uint8_t* read(size_t, size_t) {
      assert(false && "Independent-page oracle must not use a fixture read window");
      return nullptr;
    }
  };
  std::string content;
  bool failRead = false;
  size_t getFileSize() const { return content.size(); }
  bool readContent(uint8_t* out, size_t offset, size_t size) {
    ++work.reads;
    std::cout << "read " << offset << ' ' << size << ' ' << failRead << '\n';
    if (failRead) return false;
    assert(offset + size <= content.size());
    std::memcpy(out, content.data() + offset, size);
    return true;
  }
};
struct TxtDisplayLine { std::string text; uint8_t headingLevel; };
class TxtReaderActivity {
 public:
  Txt* txt;
  GfxRenderer& renderer;
  int cachedFontId = 1, viewportWidth = 400, viewportHeight = 640, linesPerPage = 18;
  bool markdownMode = false;
#ifdef TXT_HAS_READ_WINDOW
  bool loadPageAtOffset(size_t, bool, std::vector<TxtDisplayLine>&, size_t&, bool&, Txt::ReadWindow* = nullptr);
#else
  bool loadPageAtOffset(size_t, bool, std::vector<TxtDisplayLine>&, size_t&, bool&);
#endif
};
