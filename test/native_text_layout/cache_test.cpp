#include "NativeTextLayoutCache.h"

#include <cassert>
#include <iostream>
#include <random>

static bool rejectAllocation = false;

void* operator new[](std::size_t bytes, const std::nothrow_t&) noexcept {
  if (rejectAllocation) return nullptr;
  try {
    return ::operator new[](bytes);
  } catch (...) {
    return nullptr;
  }
}

int main() {
  NativeTextLayoutCache cache;
  NativeTextLayoutCache::Key key{&cache, &cache, 1, 10, 400};
  const auto accept = [](size_t) { return true; };
  const auto check = [&](const std::string& source, const std::vector<std::string>& lines) {
    assert(cache.store(key, source, lines, accept));
    assert(cache.matches(key, source.c_str()));
    assert(cache.lineCount() == lines.size());
    for (size_t i = 0; i < lines.size(); ++i) assert(lines[i] == cache.line(i));

    std::string changed = source + "x";
    assert(!cache.matches(key, changed.c_str()));
    if (!source.empty()) {
      changed = source;
      changed.back() ^= 1;
      assert(!cache.matches(key, changed.c_str()));
    }
    auto other = key;
    ++other.fontGeneration;
    assert(!cache.matches(other, source.c_str()));
    other = key;
    ++other.width;
    assert(!cache.matches(other, source.c_str()));
    other = key;
    ++other.fontId;
    assert(!cache.matches(other, source.c_str()));
    other = key;
    other.renderer = nullptr;
    assert(!cache.matches(other, source.c_str()));
    other = key;
    other.font = nullptr;
    assert(!cache.matches(other, source.c_str()));
  };

  check("", {});
  check("\n", {"", ""});
  check("a\n\n", {"a", "", ""});
  check("é 中文", {"é", "中文"});
  check(std::string(cache.kMaxSourceBytes, 'x'), {std::string(cache.kMaxSourceBytes, 'x')});
  check("x", std::vector<std::string>(cache.kMaxLines, ""));
  // One line: two uint16_t offsets, a one-byte source plus NUL, a line NUL.
  const size_t longest = cache.kMaxStorageBytes - 4 - 2 - 1;
  check("x", {std::string(longest, 'x')});
  assert(!cache.store(key, "x", {std::string(longest + 1, 'x')}, accept));
  assert(!cache.matches(key, "x"));
  assert(!cache.store(key, std::string(cache.kMaxSourceBytes + 1, 'x'), {}, accept));
  assert(!cache.store(key, "x", std::vector<std::string>(cache.kMaxLines + 1), accept));

  // Source-copy, each line-copy and final-publication checkpoints all fail closed.
  for (size_t fail = 0; fail < 6; ++fail) {
    check("foo", {"a", "b", "c", "d"});
    size_t n = 0;
    assert(!cache.store(key, "foo", {"a", "b", "c", "d"},
                        [&](size_t) { return n++ != fail; }));
    assert(cache.lineCount() == 0);
    assert(!cache.matches(key, "foo"));
  }
  check("foo", {"foo"});
  rejectAllocation = true;
  assert(!cache.store(key, "foo", {"foo"}, accept));
  assert(!cache.matches(key, "foo"));
  rejectAllocation = false;

  auto disabledKey = key;
  disabledKey.fontGeneration = 0;
  assert(!cache.store(disabledKey, "foo", {"foo"}, accept));
  assert(!cache.matches(disabledKey, "foo"));

  // The snapshot owns both input and output even after caller buffers change.
  std::string mutableSource = "hello";
  std::vector<std::string> mutableLines{"hel", "lo"};
  check(mutableSource, mutableLines);
  mutableSource[0] = 'j';
  mutableLines[0][0] = 'j';
  assert(!cache.matches(key, mutableSource.c_str()));
  assert(cache.matches(key, "hello"));
  assert(std::string(cache.line(0)) == "hel");

  std::mt19937 generator(42);
  for (int n = 0; n < 500; ++n) {
    std::string source(generator() % 16385, 'x');
    std::vector<std::string> lines;
    size_t total = source.size() + 1 + 2;
    const size_t count = generator() % 1025;
    for (size_t i = 0; i < count; ++i) {
      const size_t length = generator() % 50;
      if (total + length + 3 > cache.kMaxStorageBytes) break;
      lines.emplace_back(length, 'a' + generator() % 26);
      total += length + 3;
    }
    check(source, lines);
  }
  cache.clear();
  assert(cache.lineCount() == 0);
  assert(!cache.matches(key, ""));
  std::cout << "cache review PASS: 500 random snapshots, exact/source and all-key invalidation, "
               "16/48 KiB and 1024-line boundaries, allocation/checkpoint rejection, clear/retry\n";
}
