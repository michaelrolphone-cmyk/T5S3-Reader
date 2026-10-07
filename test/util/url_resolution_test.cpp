#include "util/UrlUtils.h"

#include <cstdlib>
#include <iostream>
#include <string>
#include <utility>

static void check(const std::string& base, const std::string& reference, const std::string& expected) {
  const auto actual = UrlUtils::buildUrl(base, reference);
  if (actual != expected) {
    std::cerr << "FAIL: base=" << base << " reference=" << reference << " expected=" << expected
              << " actual=" << actual << '\n';
    std::exit(1);
  }
}

int main() {
  // Canonical BUG-67: valid sibling navigation/acquisition links from a feed document.
  check("https://example.test/opds/root.xml", "books.xml", "https://example.test/opds/books.xml");
  check("https://example.test/opds/root.xml", "books/book.epub", "https://example.test/opds/books/book.epub");

  // RFC 3986 section 5.4 normal and abnormal reference-resolution examples.
  const std::pair<const char*, const char*> examples[] = {
      {"g:h", "g:h"}, {"g", "http://a/b/c/g"}, {"./g", "http://a/b/c/g"},
      {"g/", "http://a/b/c/g/"}, {"/g", "http://a/g"}, {"//g", "http://g"},
      {"?y", "http://a/b/c/d;p?y"}, {"g?y", "http://a/b/c/g?y"},
      {"#s", "http://a/b/c/d;p?q#s"}, {"g#s", "http://a/b/c/g#s"},
      {"g?y#s", "http://a/b/c/g?y#s"}, {";x", "http://a/b/c/;x"},
      {"g;x", "http://a/b/c/g;x"}, {"g;x?y#s", "http://a/b/c/g;x?y#s"},
      {"", "http://a/b/c/d;p?q"}, {".", "http://a/b/c/"}, {"./", "http://a/b/c/"},
      {"..", "http://a/b/"}, {"../", "http://a/b/"}, {"../g", "http://a/b/g"},
      {"../..", "http://a/"}, {"../../", "http://a/"}, {"../../g", "http://a/g"},
      {"../../../g", "http://a/g"}, {"../../../../g", "http://a/g"},
      {"/./g", "http://a/g"}, {"/../g", "http://a/g"}, {"g.", "http://a/b/c/g."},
      {".g", "http://a/b/c/.g"}, {"g..", "http://a/b/c/g.."}, {"..g", "http://a/b/c/..g"},
      {"./../g", "http://a/b/g"}, {"./g/.", "http://a/b/c/g/"},
      {"g/./h", "http://a/b/c/g/h"}, {"g/../h", "http://a/b/c/h"},
      {"g;x=1/./y", "http://a/b/c/g;x=1/y"}, {"g;x=1/../y", "http://a/b/c/y"},
      {"g?y/./x", "http://a/b/c/g?y/./x"}, {"g?y/../x", "http://a/b/c/g?y/../x"},
      {"g#s/./x", "http://a/b/c/g#s/./x"}, {"g#s/../x", "http://a/b/c/g#s/../x"},
      {"http:g", "http:g"}};
  for (const auto& example : examples) check("http://a/b/c/d;p?q", example.first, example.second);

  const std::string feed = "https://example.test/opds/root.xml?old=1#old";
  check(feed, "", "https://example.test/opds/root.xml?old=1");
  check(feed, "?", "https://example.test/opds/root.xml?");
  check(feed, "#", "https://example.test/opds/root.xml?old=1#");
  check(feed, "?#", "https://example.test/opds/root.xml?#");
  check(feed, "?next=https://other.test/a/../b", "https://example.test/opds/root.xml?next=https://other.test/a/../b");
  check(feed, "next.xml?next=https://other.test/a/../b#part/../x",
        "https://example.test/opds/next.xml?next=https://other.test/a/../b#part/../x");
  check(feed, "//other.test/library/../books.xml?x=1#s", "https://other.test/books.xml?x=1#s");
  check(feed, "HTTP://other.test/a/../b", "HTTP://other.test/b");
  check(feed, "urn:isbn:1234", "urn:isbn:1234");
  check(feed, "path//book.epub", "https://example.test/opds/path//book.epub");
  check(feed, "path//../book.epub", "https://example.test/opds/path/book.epub");
  check(feed, "book%2Fpart%20one.epub", "https://example.test/opds/book%2Fpart%20one.epub");
  check(feed, "%2e%2e/book.epub", "https://example.test/opds/%2e%2e/book.epub");
  check(feed, "./part:one.epub", "https://example.test/opds/part:one.epub");
  check(feed, "caf\xc3\xa9.epub", "https://example.test/opds/caf\xc3\xa9.epub");
  check("https://example.test/opds/", "books.xml", "https://example.test/opds/books.xml");
  check("https://example.test?old=1#old", "books.xml", "https://example.test/books.xml");
  check("https://example.test?old=1#old", "/books.xml", "https://example.test/books.xml");
  check("example.test:8080/opds/root.xml", "books.xml", "http://example.test:8080/opds/books.xml");
  check("https://[2001:db8::1]:8080/opds/root.xml", "books.xml", "https://[2001:db8::1]:8080/opds/books.xml");
  check("", "", "http://");  // Preserve the existing bare-server default; validation belongs to the caller.
  // No shared state: failures in callers/retries and unrelated calls cannot contaminate resolution.
  for (int i = 0; i < 100; ++i) {
    check(feed, "../book.epub", "https://example.test/book.epub");
    check("http://other.test/base/", "x", "http://other.test/base/x");
  }
  std::string dots;
  for (int i = 0; i < 2000; ++i) dots += "part/../";
  check(feed, dots + "book.epub", "https://example.test/opds/book.epub");
  std::cout << "URL reference resolution tests passed\n";
}
