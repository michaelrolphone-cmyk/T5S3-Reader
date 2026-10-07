#pragma once
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace Fixture {
enum class Fault { None, Open, Seek, Negative, Zero, Short, Oversized };
inline Fault fault = Fault::None;
inline size_t length = 0;
inline int failSample = 0, opens = 0, closes = 0, live = 0, seeks = 0, reads = 0;
inline std::vector<size_t> offsets, requests;
inline uint8_t byteAt(size_t offset) { return static_cast<uint8_t>((offset * 31 + (offset >> 9) + 7) & 255); }
inline void reset(size_t size, Fault failure = Fault::None, int sample = 0) {
  assert(live == 0);
  fault = failure; length = size; failSample = sample;
  opens = closes = seeks = reads = 0;
  offsets.clear(); requests.clear();
}
}

// Models the production HalFile destructor's deterministic handle cleanup.
struct FsFile {
  bool opened = false;
  size_t position = 0;
  ~FsFile() { if (opened) { ++Fixture::closes; --Fixture::live; } }
  size_t fileSize() const { assert(opened); return Fixture::length; }
  bool seekSet(size_t offset) {
    assert(opened); Fixture::offsets.push_back(offset);
    const int sample = Fixture::seeks++;
    if (Fixture::fault == Fixture::Fault::Seek && sample == Fixture::failSample) return false;
    assert(offset < Fixture::length); position = offset; return true;
  }
  int read(void* buffer, size_t count) {
    assert(opened && count > 0 && count <= 1024 && position + count <= Fixture::length);
    Fixture::requests.push_back(count);
    const int sample = Fixture::reads++;
    int result = static_cast<int>(count);
    if (sample == Fixture::failSample) {
      switch (Fixture::fault) {
        case Fixture::Fault::Negative: result = -1; break;
        case Fixture::Fault::Zero: result = 0; break;
        case Fixture::Fault::Short: result -= 1; break;
        case Fixture::Fault::Oversized: result += 1; break;
        default: break;
      }
    }
    for (size_t i = 0; i < count; ++i) static_cast<uint8_t*>(buffer)[i] = Fixture::byteAt(position + i);
    return result;
  }
};
struct StorageFixture {
  bool openFileForRead(const char*, const std::string&, FsFile& file) {
    ++Fixture::opens;
    if (Fixture::fault == Fixture::Fault::Open) return false;
    file.opened = true; ++Fixture::live; return true;
  }
};
inline StorageFixture Storage;
