#pragma once
#include <cassert>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
namespace HashFixture {
inline std::vector<uint8_t> input;
inline int begins = 0, finalizes = 0, publishes = 0;
inline void reset() { input.clear(); begins = finalizes = publishes = 0; }
inline const std::string digest = "0123456789abcdef0123456789abcdef";
}
// MD5 is an endpoint fixture: assert exact healthy input and forbid partial
// finalization. The Arduino MD5 implementation is compiled by target CI.
struct MD5Builder {
  void begin() { ++HashFixture::begins; HashFixture::input.clear(); }
  void add(const uint8_t* bytes, size_t count) {
    assert(count <= 1024); // Also catches a signed read error widened to size_t.
    HashFixture::input.insert(HashFixture::input.end(), bytes, bytes + count);
  }
  void add(const char* text) { add(reinterpret_cast<const uint8_t*>(text), std::strlen(text)); }
  void calculate() { ++HashFixture::finalizes; }
  std::string toString() { ++HashFixture::publishes; return HashFixture::digest; }
};
