#pragma once
#include <T5StreamApi.h>
#include <array>
#include <cstdint>
#include <memory>
#include "RecordQueue.h"

namespace RuntimeStreams {
// Firmware adapter vtable for file/HTTP/legacy USB backends. Never export this
// registration to an ELF. Installed providers publish buffer-backed endpoints
// instead of storing ELF function pointers. Provider callbacks must run outside
// the bridge mutex; pump() snapshots the vtable and generation first.
struct Provider {
  void* context = nullptr;
  int32_t (*read)(void*, void*, uint32_t, uint32_t*) = nullptr;
  int32_t (*write)(void*, const void*, uint32_t, uint32_t*) = nullptr;
  int32_t (*seek)(void*, uint64_t) = nullptr;
  int32_t (*finish)(void*) = nullptr;
  void (*close)(void*) = nullptr;
};
enum { kStreamPublic = 0, kStreamProtected = 1 };
// Snapshot of a single bounded provider transfer. Copied under the registry
// lock, executed without it, then committed if the generation still matches.
struct ExternalIo {
  bool pending = false;
  bool reading = false;
  unsigned pipeIndex = 0;
  uint32_t pipeGeneration = 0;
  unsigned streamIndex = 0;
  uint32_t streamGeneration = 0;
  Provider provider{};
  uint32_t request = 0;
  std::array<uint8_t, T5_STREAM_CHUNK> payload{};
};
class Registry {
 public:
  static constexpr unsigned MaxStreams = 12, MaxPipes = 4, MaxGrants = 4;
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
  // Installed-ELF endpoints are buffer-backed and never store ELF callbacks.
  // protection=kStreamProtected refuses pipes into a public sink.
  int32_t publishEndpoint(uint32_t publisher, uint32_t kind, uint32_t flags,
                          uint32_t byteCapacity, const char* schema,
                          uint32_t maxRecord, uint32_t capacityRecords,
                          uint32_t protection, t5_stream_t* out);
  int32_t grant(uint32_t publisher, t5_stream_t, uint32_t consumer, uint32_t rights);
  int32_t revoke(uint32_t publisher, t5_stream_t, uint32_t consumer);
  int32_t connectAcross(uint32_t pipeOwner, uint32_t sourceOwner, t5_stream_t source,
                        uint32_t destOwner, t5_stream_t dest, uint32_t policy, t5_pipe_t* out);
  int32_t pause(uint32_t owner, t5_pipe_t, bool);
  int32_t cancel(uint32_t owner, t5_pipe_t);
  int32_t closePipe(uint32_t owner, t5_pipe_t);
  int32_t pipeInfo(uint32_t owner, t5_pipe_t, t5_pipe_info_t*);
  void release(uint32_t owner);
  bool runnable() const;
  void pump(); // one bounded byte chunk or one atomic record per pipe, rotating first
  // Under the registry lock: advance in-memory work or fill *io for a provider
  // transfer that MUST run after the lock is dropped. Returns true when *io is
  // pending. Host tests may keep calling pump(), which runs I/O inline.
  bool pumpPrepare(ExternalIo* io);
  void pumpComplete(const ExternalIo& io, int32_t result, const void* data, uint32_t count);
 private:
  struct Grant {
    uint32_t consumer = 0;
    uint32_t rights = 0;
  };
  struct Stream {
    uint32_t generation = 0, owner = 0, flags = 0, kind = T5_STREAM_BYTES;
    Provider provider;
    std::unique_ptr<uint8_t[]> buffer;
    RecordQueue records;
    uint32_t capacity = 0, head = 0, used = 0, high = 0;
    int32_t terminal = 0;
    uint64_t read = 0, written = 0;
    uint32_t protection = kStreamPublic;
    bool elfEndpoint = false;
    Grant grants[MaxGrants]{};
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
  Stream* streamByHandle(t5_stream_t);
  Pipe* pipe(uint32_t owner, t5_pipe_t);
  bool leased(t5_stream_t, bool reading) const;
  bool allowed(const Stream&, uint32_t caller, uint32_t need) const;
  void failPipes(t5_stream_t, int32_t error);
  void clearGrants(Stream&);
  int32_t transfer(Stream&, bool reading, void*, uint32_t, uint32_t*);
  bool usesExternalProvider(const Stream&) const;
  int32_t connectInternal(uint32_t pipeOwner, Stream* s, t5_stream_t source,
                          Stream* d, t5_stream_t dest, uint32_t policy, t5_pipe_t* out);
  enum class Step { Idle, Progressed, NeedIo };
  Step pumpIndex(unsigned index, ExternalIo* io);
};
}
