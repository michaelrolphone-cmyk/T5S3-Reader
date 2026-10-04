#include <cassert>
#include <string>

// Storage/cache bytes are fixtures; the reset operation below is production code.
struct CacheFile {
  bool failClose = false;
  unsigned closes = 0;
  bool close() { ++closes; return !failClose; }
};
struct CacheStorage {
  bool failOpen = false;
  unsigned opens = 0;
  std::string path, bytes = "partial nav entries";
  bool openFileForWrite(const char*, const std::string& value, CacheFile&) {
    ++opens;
    path = value;
    if (failOpen) return false;
    bytes.clear();
    return true;
  }
} Storage;
constexpr char tmpTocBinFile[] = "/toc.bin.tmp";
class BookMetadataCache {
 public:
  bool buildMode = true;
  CacheFile tocFile;
  std::string cachePath = "/cache/book";
  int tocCount = 7;
  int spineCount = 5; // Reset must leave the completed spine pass alone.
  bool resetTocEntries();
};
#include "cache_reset.inc"
int main() {
  BookMetadataCache cache;
  cache.buildMode = false;
  assert(!cache.resetTocEntries());
  assert(cache.tocCount == 7 && cache.tocFile.closes == 0 && Storage.opens == 0);
  cache.buildMode = true;
  cache.tocFile.failClose = true;
  assert(!cache.resetTocEntries());
  assert(cache.tocCount == 7 && Storage.opens == 0);
  cache.tocFile.failClose = false;
  Storage.failOpen = true;
  assert(!cache.resetTocEntries());
  assert(cache.tocCount == 7 && Storage.opens == 1);
  Storage.failOpen = false;
  assert(cache.resetTocEntries());
  assert(cache.tocCount == 0 && cache.spineCount == 5);
  assert(Storage.bytes.empty() && Storage.path == "/cache/book/toc.bin.tmp");
  assert(cache.resetTocEntries());
  assert(cache.tocCount == 0 && cache.spineCount == 5);
}
