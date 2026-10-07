#include <cassert>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

#include "BookmarkUtil.h"

static bool validUtf8(const std::string& text) {
  for (size_t i = 0; i < text.size();) {
    const auto lead = static_cast<unsigned char>(text[i++]);
    if (lead < 0x80) continue;
    unsigned trailing;
    uint32_t value;
    uint32_t minimum;
    if (lead >= 0xc2 && lead <= 0xdf) { trailing = 1; value = lead & 0x1f; minimum = 0x80; }
    else if (lead >= 0xe0 && lead <= 0xef) { trailing = 2; value = lead & 0x0f; minimum = 0x800; }
    else if (lead >= 0xf0 && lead <= 0xf4) { trailing = 3; value = lead & 0x07; minimum = 0x10000; }
    else return false;
    if (trailing > text.size() - i) return false;
    while (trailing--) {
      const auto next = static_cast<unsigned char>(text[i++]);
      if ((next & 0xc0) != 0x80) return false;
      value = (value << 6) | (next & 0x3f);
    }
    if (value < minimum || value > 0x10ffff || (value >= 0xd800 && value <= 0xdfff)) return false;
  }
  return true;
}

int main() {
  const std::string original = std::string(71, 'a') + u8"é";
  const std::string result = BookmarkUtil::sanitizeBookmarkSummary(original);
  assert(validUtf8(result));
  assert(result == std::string(71, 'a'));

  const std::vector<std::string> characters = {u8"é", u8"Ж", u8"€", u8"漢", u8"🙂", u8"𐍈"};
  unsigned cases = 1;
  for (const auto& character : characters) {
    for (size_t prefix = 0; prefix <= 76; ++prefix) {
      const std::string source = std::string(prefix, 'x') + character + "tail";
      const std::string actual = BookmarkUtil::sanitizeBookmarkSummary(source);
      size_t length = source.size() < 72 ? source.size() : 72;
      if (prefix < 72 && prefix + character.size() > 72) length = prefix;
      assert(actual == source.substr(0, length));
      assert(actual.size() <= 72 && validUtf8(actual));
      assert(BookmarkUtil::sanitizeBookmarkSummary(actual) == actual);
      ++cases;
    }
    for (size_t repetitions = 1; repetitions <= 80; ++repetitions) {
      std::string source;
      for (size_t i = 0; i < repetitions; ++i) source += character;
      const std::string actual = BookmarkUtil::sanitizeBookmarkSummary(source);
      const size_t expected = (source.size() < 72 ? source.size() : 72 / character.size() * character.size());
      assert(actual == source.substr(0, expected) && validUtf8(actual));
      ++cases;
    }
  }
  assert(BookmarkUtil::sanitizeBookmarkSummary("").empty());
  assert(BookmarkUtil::sanitizeBookmarkSummary(" \t\n\r\v\f ").empty());
  assert(BookmarkUtil::sanitizeBookmarkSummary("  alpha  beta  ") == "alpha beta");
  assert(BookmarkUtil::sanitizeBookmarkSummary("line\nnext") == "linenext");
  assert(BookmarkUtil::sanitizeBookmarkSummary(u8" \t é  漢\n🙂 \r ") == u8"é 漢🙂");
  assert(BookmarkUtil::sanitizeBookmarkSummary(std::string(72, 'a')) == std::string(72, 'a'));
  assert(BookmarkUtil::sanitizeBookmarkSummary(std::string(73, 'a')) == std::string(72, 'a'));

  // Every byte can safely reach ctype classification, even malformed input.
  // Repairing malformed source encoding is not part of this truncation contract.
  for (unsigned byte = 0; byte <= 255; ++byte) {
    std::string source(80, static_cast<char>(byte));
    source = "  " + source + "  ";
    assert(BookmarkUtil::sanitizeBookmarkSummary(source).size() <= 72);
    ++cases;
  }
  // Repeated calls have no parser/stream state to retain after empty/error-like input.
  for (int i = 0; i < 100; ++i) {
    assert(BookmarkUtil::sanitizeBookmarkSummary(original) == result);
    assert(BookmarkUtil::sanitizeBookmarkSummary("").empty());
    assert(BookmarkUtil::sanitizeBookmarkSummary("retry") == "retry");
  }
  std::cout << "Bookmark summary regressions passed (" << cases
            << " boundary/byte cases plus whitespace and repeated-call checks)\n";
}
