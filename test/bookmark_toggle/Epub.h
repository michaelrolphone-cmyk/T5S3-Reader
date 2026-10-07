#pragma once
#include <Print.h>
#include <cstdint>
#include <string>

// Cached spine metadata fixture; the tested progress/stream methods are extracted
// unchanged from production Epub.cpp. This does not model physical SD latency.
class Epub {
 public:
  std::string filepath;
  size_t chapterSize = 0;
  struct SpineItem {
    std::string href;
  };
  int getSpineItemsCount() const { return 1; }
  SpineItem getSpineItem(int) const { return {"OPS/ch.xhtml"}; }
  size_t getBookSize() const { return chapterSize; }
  size_t getCumulativeSpineItemSize(int) const { return chapterSize; }
  const std::string& getPath() const { return filepath; }
  bool readItemContentsToStream(const std::string&, Print&, size_t) const;
  bool getItemSize(const std::string&, size_t*) const;
  float calculateProgress(int, float) const;
};
