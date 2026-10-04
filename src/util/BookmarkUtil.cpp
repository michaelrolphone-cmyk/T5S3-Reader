#include "BookmarkUtil.h"

#include <Utf8.h>

#include <algorithm>
#include <cctype>
#include <string>

std::string BookmarkUtil::getBookmarksDir() { return "/.crosspoint/bookmarks/"; }

std::string BookmarkUtil::getBookmarkPath(const std::string& bookPath) {
  std::string bookName = bookPath;
  if (!bookName.empty() && (bookName.front() == '/' || bookName.front() == '\\')) {
    bookName.erase(0, 1);
  }

  std::replace(bookName.begin(), bookName.end(), '/', '_');
  std::replace(bookName.begin(), bookName.end(), '\\', '_');

  const size_t lastDot = bookName.find_last_of('.');
  if (lastDot != std::string::npos) {
    bookName.erase(lastDot);
  }

  bookName += ".json";
  return getBookmarksDir() + bookName;
}

std::string BookmarkUtil::sanitizeBookmarkSummary(std::string summary) {
  summary.erase(
      std::unique(summary.begin(), summary.end(),
                  [](unsigned char a, unsigned char b) { return std::isspace(a) && std::isspace(b); }),
      summary.end());
  summary.erase(std::remove(summary.begin(), summary.end(), '\n'), summary.end());
  summary.erase(summary.begin(),
                std::find_if(summary.begin(), summary.end(),
                             [](unsigned char ch) { return !std::isspace(ch); }));
  summary.erase(
      std::find_if(summary.rbegin(), summary.rend(),
                   [](unsigned char ch) { return !std::isspace(ch); })
          .base(),
      summary.end());

  if (summary.size() > 72) {
    summary.resize(utf8SafeTruncateBuffer(summary.c_str(), 72));
  }

  return summary;
}
