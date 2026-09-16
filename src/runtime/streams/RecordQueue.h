#pragma once

#include <T5StreamApi.h>
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <memory>
#include <new>

// Firmware-owned, allocation-bounded record buffering primitive. The Stream
// Registry will own one of these per typed stream; this is not an independent
// application-visible registry or an alternate ELF ABI. Serialization and
// execution-context ownership are provided by the encompassing registry.
namespace RuntimeStreams {

class RecordQueue final {
 public:
  static constexpr uint32_t MaxSchema = 64;
  static constexpr uint32_t MaxRecord = T5_STREAM_CHUNK;
  static constexpr uint32_t MaxQueued = 8;
  static constexpr uint32_t MaxStorage = T5_STREAM_MAX_BUFFER;

  struct Stats {
    uint32_t max_record = 0, capacity_records = 0, queued_records = 0;
    uint32_t queued_bytes = 0, high_water_records = 0, high_water_bytes = 0;
    uint64_t records_read = 0, records_written = 0;
    uint64_t bytes_read = 0, bytes_written = 0;
    int32_t terminal = 0;
  };

  // Stable, explicit, versioned ASCII type names: location.fix.v1, etc.
  // Never use a schema pointer owned by a dynamically unloaded ELF.
  static bool validSchema(const char* name) {
    if (!name) return false;
    const size_t n = strnlen(name, MaxSchema);
    if (n < 4 || n >= MaxSchema || name[0] < 'a' || name[0] > 'z') return false;
    size_t version = n;
    for (size_t i = 0; i < n; ++i) {
      const char c = name[i];
      if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.'))
        return false;
      if (c == '.' && (i == 0 || i + 1 == n || name[i - 1] == '.')) return false;
      if (c == '.' && i + 2 < n && name[i + 1] == 'v' && name[i + 2] >= '1' && name[i + 2] <= '9')
        version = i;
    }
    if (version == n || version < 1 || version + 2 >= n) return false;
    for (size_t i = version + 2; i < n; ++i)
      if (name[i] < '0' || name[i] > '9') return false;
    return true;
  }

  static bool compatible(const char* source, const char* destination) {
    return validSchema(source) && validSchema(destination) && std::strcmp(source, destination) == 0;
  }

  int32_t configure(const char* schema, uint32_t maxRecord, uint32_t slots) {
    if (active_) return T5_STREAM_BUSY;
    if (!validSchema(schema) || !maxRecord || maxRecord > MaxRecord ||
        !slots || slots > MaxQueued || slots > MaxStorage / maxRecord)
      return T5_STREAM_INVALID;
    std::unique_ptr<uint8_t[]> data(new (std::nothrow) uint8_t[maxRecord * slots]);
    if (!data) return T5_STREAM_LIMIT;
    std::memcpy(schema_, schema, std::strlen(schema) + 1);
    data_ = std::move(data);
    max_record_ = maxRecord;
    slots_ = slots;
    active_ = true;
    return T5_STREAM_OK;
  }

  const char* schema() const { return active_ ? schema_ : nullptr; }
  bool configured() const { return active_; }
  uint32_t maxRecord() const { return max_record_; }
  uint32_t capacity() const { return slots_; }
  uint32_t queued() const { return used_; }

  // Entire records are accepted or rejected; no prefix is ever committed.
  // A zero-byte record is valid and distinguishable from AGAIN by the result.
  int32_t write(const void* bytes, uint32_t size) {
    if (!active_ || (!bytes && size) || size > max_record_) return T5_STREAM_INVALID;
    if (terminal_) return terminal_ == T5_STREAM_EOF ? T5_STREAM_CLOSED : terminal_;
    if (used_ == slots_) return T5_STREAM_AGAIN;
    const uint32_t index = (head_ + used_) % slots_;
    if (size) std::memcpy(data_.get() + index * max_record_, bytes, size);
    lengths_[index] = static_cast<uint16_t>(size);
    ++used_;
    queued_bytes_ += size;
    high_records_ = std::max(high_records_, used_);
    high_bytes_ = std::max(high_bytes_, queued_bytes_);
    ++records_written_;
    bytes_written_ += size;
    return T5_STREAM_OK;
  }

  int32_t read(void* out, uint32_t capacity, uint32_t* size) {
    if (size) *size = 0;
    if (!active_ || !size || (!out && capacity)) return T5_STREAM_INVALID;
    if (!used_) return terminal_ ? terminal_ : T5_STREAM_AGAIN;
    const uint32_t length = lengths_[head_];
    if (capacity < length || (!out && length)) return T5_STREAM_LIMIT;
    if (length) std::memcpy(out, data_.get() + head_ * max_record_, length);
    *size = length;
    head_ = (head_ + 1) % slots_;
    --used_;
    queued_bytes_ -= length;
    ++records_read_;
    bytes_read_ += length;
    return T5_STREAM_OK;
  }

  int32_t finish(int32_t terminal = T5_STREAM_EOF) {
    if (!active_ || (terminal != T5_STREAM_EOF && terminal >= 0)) return T5_STREAM_INVALID;
    if (terminal_) return terminal_ == T5_STREAM_EOF ? T5_STREAM_OK : terminal_;
    terminal_ = terminal;
    return T5_STREAM_OK;
  }

  Stats stats() const {
    return {max_record_, slots_, used_, queued_bytes_, high_records_, high_bytes_,
            records_read_, records_written_, bytes_read_, bytes_written_, terminal_};
  }

  // Explicitly erase prior state before a slot is returned to the generation-
  // safe stream handle registry. A reused queue never inherits old records.
  void reset() {
    data_.reset();
    std::memset(schema_, 0, sizeof(schema_));
    std::memset(lengths_, 0, sizeof(lengths_));
    active_ = false;
    max_record_ = slots_ = head_ = used_ = queued_bytes_ = high_records_ = high_bytes_ = 0;
    records_read_ = records_written_ = bytes_read_ = bytes_written_ = 0;
    terminal_ = 0;
  }

  RecordQueue() = default;
  ~RecordQueue() = default;
  RecordQueue(const RecordQueue&) = delete;
  RecordQueue& operator=(const RecordQueue&) = delete;

 private:
  std::unique_ptr<uint8_t[]> data_;
  char schema_[MaxSchema]{};
  uint16_t lengths_[MaxQueued]{};
  uint32_t max_record_ = 0, slots_ = 0, head_ = 0, used_ = 0;
  uint32_t queued_bytes_ = 0, high_records_ = 0, high_bytes_ = 0;
  uint64_t records_read_ = 0, records_written_ = 0, bytes_read_ = 0, bytes_written_ = 0;
  int32_t terminal_ = 0;
  bool active_ = false;
};

}  // namespace RuntimeStreams
