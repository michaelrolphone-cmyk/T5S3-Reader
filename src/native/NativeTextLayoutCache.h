#pragma once

#include <cstdint>
#include <cstring>
#include <memory>
#include <new>
#include <string>
#include <vector>

// One optional snapshot, owned by the current native UI invocation. Admission
// limits affect reuse only: every rejection keeps the original rendering path.
class NativeTextLayoutCache {
 public:
  static constexpr size_t kMaxSourceBytes = 16 * 1024;
  static constexpr size_t kMaxLines = 1024;
  static constexpr size_t kMaxStorageBytes = 48 * 1024;
  struct Key {
    const void* renderer = nullptr;
    const void* font = nullptr;
    uint64_t fontGeneration = 0;
    int fontId = 0;
    int width = 0;
    bool operator==(const Key& other) const {
      return renderer == other.renderer && font == other.font &&
             fontGeneration == other.fontGeneration && fontId == other.fontId && width == other.width;
    }
  };

  void clear() { storage_.reset(); sourceBytes_ = lineCount_ = 0; key_ = {}; }
  bool matches(const Key& key, const char* text) const {
    if (!storage_ || !key.fontGeneration || !(key == key_)) return false;
    // App buffers can mutate in place. Compare actual bytes, never just their
    // address or a collision-prone hash. The probe is bounded even on a miss.
    size_t length = 0;
    while (length <= kMaxSourceBytes && text[length]) ++length;
    return length == sourceBytes_ && std::memcmp(source(), text, length) == 0;
  }
  size_t lineCount() const { return lineCount_; }
  const char* line(size_t index) const {
    uint16_t offset = 0;
    std::memcpy(&offset, storage_.get() + index * sizeof(offset), sizeof(offset));
    return source() + sourceBytes_ + 1 + offset;
  }

  template <typename Checkpoint>
  bool store(const Key& key, const std::string& text, const std::vector<std::string>& lines,
             Checkpoint checkpoint) {
    clear();
    if (!key.fontGeneration || text.size() > kMaxSourceBytes || lines.size() > kMaxLines) return false;
    const size_t tableBytes = (lines.size() + 1) * sizeof(uint16_t);
    size_t bytes = tableBytes + text.size() + 1;
    for (const auto& line : lines) {
      if (line.size() >= kMaxStorageBytes - bytes) return false;
      bytes += line.size() + 1;
    }
    // A failed optional allocation never replaces a valid frame or changes
    // its error policy. No std::string/vector allocation is added here.
    std::unique_ptr<uint8_t[]> next(new (std::nothrow) uint8_t[bytes]);
    if (!next) return false;
    auto* content = reinterpret_cast<char*>(next.get() + tableBytes);
    std::memcpy(content, text.c_str(), text.size() + 1);
    if (!checkpoint(text.size() + 1)) return false;
    char* output = content + text.size() + 1;
    size_t used = 0;
    for (size_t i = 0; i < lines.size(); ++i) {
      if (!checkpoint(lines[i].size() + 1)) return false;
      const uint16_t offset = static_cast<uint16_t>(used);
      std::memcpy(next.get() + i * sizeof(offset), &offset, sizeof(offset));
      std::memcpy(output + used, lines[i].c_str(), lines[i].size() + 1);
      used += lines[i].size() + 1;
    }
    const uint16_t end = static_cast<uint16_t>(used);
    std::memcpy(next.get() + lines.size() * sizeof(end), &end, sizeof(end));
    if (!checkpoint(0)) return false;
    key_ = key;
    sourceBytes_ = text.size();
    lineCount_ = lines.size();
    storage_ = std::move(next);
    return true;
  }

 private:
  const char* source() const {
    return reinterpret_cast<const char*>(storage_.get() + (lineCount_ + 1) * sizeof(uint16_t));
  }
  std::unique_ptr<uint8_t[]> storage_;
  Key key_{};
  size_t sourceBytes_ = 0, lineCount_ = 0;
};
