// Production browser methods are inserted by url_resolution_test.py.
#include "util/UrlUtils.h"
#include <cassert>
#include <cstddef>
#include <functional>
#include <iostream>
#include <string>
#include <utility>
#include <vector>
#define LOG_DBG(...) ((void)0)
#define STR_NO_SERVER_URL "no server"
#define STR_FETCH_FEED_FAILED "fetch failed"
#define STR_PARSE_FEED_FAILED "parse failed"
#define STR_PREV_PAGE "previous"
#define STR_NEXT_PAGE "next"
#define STR_NO_ENTRIES "empty"
#define STR_LOADING "loading"
#define STR_DOWNLOAD_FAILED "download failed"
static const char* tr(const char* text) { return text; }
enum class OpdsEntryType { BOOK, NAVIGATION };
struct OpdsEntry { OpdsEntryType type; std::string title, author, href, other; };
static const OpdsEntry child{OpdsEntryType::NAVIGATION, "Books", "", "shelves/first.xml", ""};
struct OpdsParser {
  static bool valid;
  explicit operator bool() const { return valid; }
  std::string getSearchTemplate() const { return {}; }
  std::string getNextPageUrl() const { return {}; }
  std::string getPrevPageUrl() const { return {}; }
  std::vector<OpdsEntry> getEntries() const { return {child}; }
};
bool OpdsParser::valid = true;
struct OpdsParserStream {
  static int active;
  explicit OpdsParserStream(OpdsParser&) { ++active; }
  ~OpdsParserStream() { --active; }
};
int OpdsParserStream::active = 0;
namespace HttpDownloader {
enum Result { OK, FAILED };
static bool fetched = true;
static Result downloaded = OK;
static std::string lastFetch, lastDownload;
bool fetchUrl(const std::string& url, OpdsParserStream&, const std::string&, const std::string&) {
  lastFetch = url;
  return fetched;
}
Result downloadToFile(const std::string& url, const std::string&, const std::function<void(size_t, size_t)>& progress,
                      const std::string&, const std::string&) {
  lastDownload = url;
  progress(4, 4);
  return downloaded;
}
}  // namespace HttpDownloader
namespace StringUtils {
std::string sanitizeFilename(const std::string& value) { return value; }
}
struct Epub {
  static int clears;
  Epub(const std::string&, const std::string&) {}
  void clearCache() { ++clears; }
};
int Epub::clears = 0;
namespace RuntimeNetwork {
static int shutdowns = 0;
void shutdown() { ++shutdowns; }
}
struct Activity { void onExit() {} };
struct OpdsBookBrowserActivity : Activity {
  enum class BrowserState { ERROR, LOADING, DOWNLOADING, BROWSING };
  struct Server { std::string url = "https://example.test/opds/root.xml?old=1", username, password; } server;
  BrowserState state = BrowserState::BROWSING;
  std::string currentPath, errorMessage, statusMessage, searchTemplate;
  std::vector<std::string> navigationHistory;
  std::vector<OpdsEntry> entries;
  int selectorIndex = 0, homes = 0;
  size_t downloadProgress = 0, downloadTotal = 0;
  void requestUpdate(bool = false) {}
  void onGoHome() { ++homes; }
  void onExit();
  void fetchFeed(const std::string&);
  void navigateToEntry(const OpdsEntry&);
  void navigateBack();
  void downloadBook(const OpdsEntry&);
};
// PRODUCTION_METHODS
int main() {
  using State = OpdsBookBrowserActivity::BrowserState;
  OpdsBookBrowserActivity browser;
  browser.fetchFeed("");
  assert(HttpDownloader::lastFetch == browser.server.url && browser.state == State::BROWSING);
  browser.navigateToEntry(child);
  assert(HttpDownloader::lastFetch == "https://example.test/opds/shelves/first.xml");
  browser.navigateToEntry({OpdsEntryType::NAVIGATION, "Next", "", "?page=2", ""});
  assert(HttpDownloader::lastFetch == "https://example.test/opds/shelves/first.xml?page=2");
  const OpdsEntry book{OpdsEntryType::BOOK, "Book", "Author", "../books/book.epub?download=1", ""};
  browser.downloadBook(book);
  assert(HttpDownloader::lastDownload == "https://example.test/opds/books/book.epub?download=1");
  assert(browser.state == State::BROWSING && Epub::clears == 1 && browser.downloadProgress == 4);
  HttpDownloader::downloaded = HttpDownloader::FAILED;
  browser.downloadBook(book);
  assert(browser.state == State::ERROR && Epub::clears == 1);
  HttpDownloader::downloaded = HttpDownloader::OK;
  browser.downloadBook(book);
  assert(browser.state == State::BROWSING && Epub::clears == 2);
  browser.navigateBack();
  assert(HttpDownloader::lastFetch == "https://example.test/opds/shelves/first.xml");
  HttpDownloader::fetched = false;
  browser.navigateToEntry({OpdsEntryType::NAVIGATION, "Other", "", "second.xml", ""});
  const auto retryUrl = browser.currentPath;
  assert(retryUrl == "https://example.test/opds/shelves/second.xml" && browser.state == State::ERROR);
  assert(OpdsParserStream::active == 0 && browser.entries.empty());
  HttpDownloader::fetched = true;
  browser.fetchFeed(browser.currentPath);  // Same production retry operation used by ERROR-state navigation.
  assert(HttpDownloader::lastFetch == retryUrl && browser.state == State::BROWSING);
  OpdsParser::valid = false;
  browser.fetchFeed(browser.currentPath);
  assert(browser.state == State::ERROR && OpdsParserStream::active == 0);
  OpdsParser::valid = true;
  browser.fetchFeed(browser.currentPath);
  assert(browser.state == State::BROWSING && HttpDownloader::lastFetch == retryUrl);
  browser.navigateBack();
  browser.navigateBack();
  assert(HttpDownloader::lastFetch == browser.server.url && browser.navigationHistory.empty());
  browser.navigateBack();
  assert(browser.homes == 1);
  browser.navigateToEntry(child);
  browser.onExit();
  assert(browser.entries.empty() && browser.navigationHistory.empty() && RuntimeNetwork::shutdowns == 1);
  browser.server.url.clear();
  browser.fetchFeed("");
  assert(browser.state == State::ERROR && OpdsParserStream::active == 0);
  std::cout << "Production OPDS URL navigation/download/error/retry/cleanup tests passed\n";
}
