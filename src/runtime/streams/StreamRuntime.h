#pragma once
#include <T5StreamApi.h>
#include <array>
#include <cstdint>
#include <memory>
#include "RecordQueue.h"

namespace RuntimeStreams {
// Firmware-only provider vtable. Never export registration to an ELF. Calls
// must be bounded; the bridge serializes registry operations and scheduling.
struct Provider {
  void* context = nullptr;
  int32_t (*read)(void*, void*, uint32_t, uint32_t*) = nullptr;
  int32_t (*write)(void*, const void*, uint32_t, uint32_t*) = nullptr;
  int32_t (*seek)(void*, uint64_t) = nullptr;
  int32_t (*finish)(void*) = nullptr;
  void (*close)(void*) = nullptr;
};
class Registry {
 public:
  static constexpr unsigned MaxStreams = 12, MaxPipes = 4;
  int32_t buffer(uint32_t owner, uint32_t capacity, t5_stream_t* out, uint32_t flags = 3);
  // Records share the same slots/handle generations, owner, leases and scheduler
  // as byte streams. No alternate stream registry or ELF-facing API is created.
  int32_t recordBuffer(uint32_t owner, const char* schema, uint32_t maxRecord,
                       uint32_t capacityRecords, t5_stream_t* out, uint32_t flags = 3);
  int32_t readRecord(uint32_t owner, t5_stream_t, void*, uint32_t, uint32_t*);
  int32_t writeRecord(uint32_t owner, t5_stream_t, const void*, uint32_t);
  // Firmware-only ingestion for a read-only provider endpoint. The caller must
  // already hold the registry mutex and the owning execution-context identity.
  // This bypasses only the public WRITE right, not owner, kind, size, terminal,
  // or bounded-backpressure validation. Never expose this method through ELF ABI.
  int32_t produceRecord(uint32_t owner, t5_stream_t h, const void* data, uint32_t size) {
    auto* s = stream(owner, h);
    if (!s) return T5_STREAM_INVALID;
    if (s->kind != T5_STREAM_RECORDS) return T5_STREAM_UNSUPPORTED;
    if (!(s->flags & T5_STREAM_READ)) return T5_STREAM_DENIED;
    if (leased(h, false)) return T5_STREAM_BUSY;
    const auto result = s->records.write(data, size);
    if (result == T5_STREAM_OK) s->written += size;
    return result;
  }
  // Metadata is copied to caller storage, not returned as a persistent pointer.
  int32_t recordInfo(uint32_t owner, t5_stream_t, char* schema, uint32_t schemaCapacity,
                     RecordQueue::Stats* out);
  int32_t produce(uint32_t owner, t5_stream_t, const void*, uint32_t, uint32_t*);
  int32_t attach(uint32_t owner, uint32_t kind, uint32_t flags, Provider provider, t5_stream_t* out);
  int32_t read(uint32_t owner, t5_stream_t, void*, uint32_t, uint32_t*);
  int32_t write(uint32_t owner, t5_stream_t, const void*, uint32_t, uint32_t*);
  int32_t finish(uint32_t owner, t5_stream_t, int32_t terminal = T5_STREAM_EOF);
  int32_t seek(uint32_t owner, t5_stream_t, uint64_t);
  int32_t close(uint32_t owner, t5_stream_t);
  int32_t info(uint32_t owner, t5_stream_t, t5_stream_info_t*);
  int32_t connect(uint32_t owner, t5_stream_t, t5_stream_t, uint32_t, t5_pipe_t*);
  int32_t pause(uint32_t owner, t5_pipe_t, bool);
  int32_t cancel(uint32_t owner, t5_pipe_t);
  int32_t closePipe(uint32_t owner, t5_pipe_t);
  int32_t pipeInfo(uint32_t owner, t5_pipe_t, t5_pipe_info_t*);
  void release(uint32_t owner);
  bool runnable() const;
  void pump(); // one bounded byte chunk or one atomic record per pipe, rotating first
 private:
  struct Stream {
    uint32_t generation = 0, owner = 0, flags = 0, kind = T5_STREAM_BYTES;
    Provider provider;
    std::unique_ptr<uint8_t[]> buffer;
    RecordQueue records;
    uint32_t capacity = 0, head = 0, used = 0, high = 0;
    int32_t terminal = 0;
    uint64_t read = 0, written = 0;
  };
  struct Pipe {
    uint32_t generation = 0, owner = 0, state = 0;
    t5_stream_t source = 0, destination = 0;
    std::array<uint8_t, T5_STREAM_CHUNK> data{};
    uint32_t offset = 0, used = 0;
    bool recordPending = false; // needed to stage even an empty record atomically
    int32_t error = 0;
    uint64_t transferred = 0, stalls = 0;
  };
  std::array<Stream, MaxStreams> streams_{};
  std::array<Pipe, MaxPipes> pipes_{};
  unsigned first_ = 0;
  Stream* stream(uint32_t owner, t5_stream_t);
  Pipe* pipe(uint32_t owner, t5_pipe_t);
  bool leased(t5_stream_t, bool reading) const;
  int32_t transfer(Stream&, bool reading, void*, uint32_t, uint32_t*);
};
}
