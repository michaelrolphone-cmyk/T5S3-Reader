#pragma once
#include <T5StreamApi.h>
#include <array>
#include <cstdint>
#include <memory>

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
  void pump(); // one bounded read + write per pipe, rotating first pipe
 private:
  struct Stream {
    uint32_t generation = 0, owner = 0, flags = 0;
    Provider provider;
    std::unique_ptr<uint8_t[]> buffer;
    uint32_t capacity = 0, head = 0, used = 0, high = 0;
    int32_t terminal = 0;
    uint64_t read = 0, written = 0;
  };
  struct Pipe {
    uint32_t generation = 0, owner = 0, state = 0;
    t5_stream_t source = 0, destination = 0;
    std::array<uint8_t, T5_STREAM_CHUNK> data{};
    uint32_t offset = 0, used = 0;
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
