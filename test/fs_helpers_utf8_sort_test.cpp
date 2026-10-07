#include <algorithm>
#include <cctype>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "../lib/FsHelpers/FsHelpers.h"

namespace {
int checkedLower(int value) {
  if (value != EOF && (value < 0 || value > UCHAR_MAX)) {
    std::fprintf(stderr, "ctype received invalid signed UTF-8 byte: %d\n", value);
    std::abort();
  }
  if (value >= 'A' && value <= 'Z') return value + ('a' - 'A');
  return value;
}
int checkedDigit(int value) {
  if (value != EOF && (value < 0 || value > UCHAR_MAX)) {
    std::fprintf(stderr, "ctype received invalid signed UTF-8 byte: %d\n", value);
    std::abort();
  }
  return value >= '0' && value <= '9';
}
}  // namespace

// This shim catches invalid signed-byte input even when host libc tolerates it.
#define tolower(value) checkedLower(value)
#define isdigit(value) checkedDigit(value)
#include "../lib/FsHelpers/FsHelpers.cpp"
#undef tolower
#undef isdigit

int main() {
  std::vector<std::string> names = {
      "über.bmp", "plain20.bmp", "漢字.bmp", "plain3.bmp", "éclair.bmp",
      "😀.bmp", "album/", "ALPHA2.bmp", "alpha10.bmp"};
  FsHelpers::sortFileList(names);
  const std::vector<std::string> expected = {
      "album/", "ALPHA2.bmp", "alpha10.bmp", "plain3.bmp", "plain20.bmp",
      "éclair.bmp", "über.bmp", "漢字.bmp", "😀.bmp"};
  if (names != expected) {
    std::fprintf(stderr, "filename order differs\n");
    for (const auto& name : names) std::fprintf(stderr, "%s\n", name.c_str());
    return 1;
  }
  return 0;
}
