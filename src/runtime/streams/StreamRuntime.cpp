#include "StreamRuntime.h"
#include <algorithm>
#include <cstring>
#include <new>

namespace RuntimeStreams {
namespace {
constexpr uint32_t MaxGeneration = 0xffffff;
uint32_t handle(uint32_t generation, unsigned index) { return (generation << 8) | (index + 1); }
bool live(uint32_t state) { return state == T5_PIPE_RUNNING || state == T5_PIPE_PAUSED; }
}
Registry::Stream* Registry::stream(uint32_t owner, t5_stream_t h) {
  unsigned i = (h & 255) - 1;
  if (!owner || i >= MaxStreams) return nullptr;
  auto& s = streams_[i];
  return s.owner == owner && s.generation == (h >> 8) ? &s : nullptr;
}
Registry::Pipe* Registry::pipe(uint32_t owner, t5_pipe_t h) {
  unsigned i = (h & 255) - 1;
  if (!owner || i >= MaxPipes) return nullptr;
  auto& p = pipes_[i];
  return p.owner == owner && p.generation == (h >> 8) ? &p : nullptr;
}
int32_t Registry::attach(uint32_t owner, uint32_t kind, uint32_t flags, Provider provider, t5_stream_t* out) {
  if (out) *out = 0;
  if (!out || !owner || !(flags & 3) || (flags & ~7u)) return T5_STREAM_INVALID;
  if (kind != T5_STREAM_BYTES) return T5_STREAM_UNSUPPORTED;
  for (unsigned i = 0; i < MaxStreams; ++i) {
    auto& s = streams_[i];
    if (s.owner || s.generation == MaxGeneration) continue;
    ++s.generation; s.owner = owner; s.flags = flags; s.provider = provider;
    s.terminal = 0; s.capacity = s.head = s.used = s.high = 0; s.read = s.written = 0;
    *out = handle(s.generation, i); return T5_STREAM_OK;
  }
  return T5_STREAM_LIMIT;
}
int32_t Registry::buffer(uint32_t owner, uint32_t capacity, t5_stream_t* out, uint32_t flags) {
  if (out) *out = 0;
  if (!capacity || capacity > T5_STREAM_MAX_BUFFER || !out) return T5_STREAM_INVALID;
  std::unique_ptr<uint8_t[]> bytes(new (std::nothrow) uint8_t[capacity]);
  if (!bytes) return T5_STREAM_LIMIT;
  auto result = attach(owner, T5_STREAM_BYTES, flags, {}, out);
  if (result != T5_STREAM_OK) return result;
  auto* s = stream(owner, *out); s->buffer = std::move(bytes); s->capacity = capacity;
  return T5_STREAM_OK;
}
int32_t Registry::produce(uint32_t owner, t5_stream_t h, const void* data, uint32_t size, uint32_t* count) {
  if (count) *count = 0;
  auto* s = stream(owner, h);
  if (!s || !s->buffer || !count || (!data && size)) return T5_STREAM_INVALID;
  auto flags = s->flags; s->flags |= T5_STREAM_WRITE;
  auto r = transfer(*s, false, const_cast<void*>(data), size, count);
  s->flags = flags; return r;
}
bool Registry::leased(t5_stream_t h, bool reading) const {
  for (const auto& p : pipes_) if (p.owner && live(p.state) && (reading ? p.source : p.destination) == h) return true;
  return false;
}
int32_t Registry::transfer(Stream& s, bool reading, void* data, uint32_t size, uint32_t* count) {
  *count = 0;
  if (!(s.flags & (reading ? T5_STREAM_READ : T5_STREAM_WRITE))) return T5_STREAM_DENIED;
  if (!size) return T5_STREAM_OK;
  size = std::min(size, T5_STREAM_CHUNK);
  if (!reading && s.terminal) return s.terminal == T5_STREAM_EOF ? T5_STREAM_CLOSED : s.terminal;
  int32_t result;
  if (s.buffer) {
    if (reading) {
      *count = std::min(size, s.used);
      for (uint32_t i = 0; i < *count; ++i) static_cast<uint8_t*>(data)[i] = s.buffer[(s.head + i) % s.capacity];
      s.head = (s.head + *count) % s.capacity; s.used -= *count;
      result = *count ? T5_STREAM_OK : (s.terminal ? s.terminal : T5_STREAM_AGAIN);
    } else {
      *count = std::min(size, s.capacity - s.used);
      for (uint32_t i = 0; i < *count; ++i) s.buffer[(s.head + s.used + i) % s.capacity] = static_cast<uint8_t*>(data)[i];
      s.used += *count; s.high = std::max(s.high, s.used);
      result = *count ? T5_STREAM_OK : T5_STREAM_AGAIN;
    }
  } else if (reading) {
    if (s.terminal) return s.terminal;
    result = s.provider.read ? s.provider.read(s.provider.context, data, size, count) : T5_STREAM_UNSUPPORTED;
  } else {
    result = s.provider.write ? s.provider.write(s.provider.context, data, size, count) : T5_STREAM_UNSUPPORTED;
  }
  if (*count > size) { *count = 0; result = T5_STREAM_IO; }
  if (result < 0 || result == T5_STREAM_EOF) s.terminal = result;
  if (reading) s.read += *count; else s.written += *count;
  return result;
}
int32_t Registry::read(uint32_t owner, t5_stream_t h, void* data, uint32_t size, uint32_t* count) {
  if (count) *count = 0;
  auto* s = stream(owner, h);
  if (!s || !count || (!data && size)) return T5_STREAM_INVALID;
  if (leased(h, true)) return T5_STREAM_BUSY;
  return transfer(*s, true, data, size, count);
}
int32_t Registry::write(uint32_t owner, t5_stream_t h, const void* data, uint32_t size, uint32_t* count) {
  if (count) *count = 0;
  auto* s = stream(owner, h);
  if (!s || !count || (!data && size)) return T5_STREAM_INVALID;
  if (leased(h, false)) return T5_STREAM_BUSY;
  return transfer(*s, false, const_cast<void*>(data), size, count);
}
int32_t Registry::finish(uint32_t owner, t5_stream_t h, int32_t terminal) {
  auto* s = stream(owner, h);
  if (!s || (terminal != T5_STREAM_EOF && terminal >= 0)) return T5_STREAM_INVALID;
  if (leased(h, false)) return T5_STREAM_BUSY;
  if (s->terminal) return s->terminal == T5_STREAM_EOF ? T5_STREAM_OK : s->terminal;
  if (s->provider.finish && terminal == T5_STREAM_EOF) {
    auto r = s->provider.finish(s->provider.context); if (r != T5_STREAM_OK) { s->terminal = r; return r; }
  }
  s->terminal = terminal; return T5_STREAM_OK;
}
int32_t Registry::seek(uint32_t owner, t5_stream_t h, uint64_t offset) {
  auto* s = stream(owner, h);
  if (!s) return T5_STREAM_INVALID;
  if (leased(h, true) || leased(h, false)) return T5_STREAM_BUSY;
  if (!(s->flags & T5_STREAM_SEEK) || !s->provider.seek) return T5_STREAM_UNSUPPORTED;
  auto r = s->provider.seek(s->provider.context, offset);
  if (r == T5_STREAM_OK) s->terminal = 0;
  return r;
}
int32_t Registry::close(uint32_t owner, t5_stream_t h) {
  auto* s = stream(owner, h);
  if (!s) return T5_STREAM_INVALID;
  for (auto& p : pipes_) if (p.owner && live(p.state) && (p.source == h || p.destination == h)) {
    p.state = T5_PIPE_FAILED; p.error = T5_STREAM_CLOSED; p.used = p.offset = 0;
  }
  if (s->provider.close) s->provider.close(s->provider.context);
  s->buffer.reset(); s->provider = {}; s->owner = 0;
  return T5_STREAM_OK;
}
int32_t Registry::info(uint32_t owner, t5_stream_t h, t5_stream_info_t* out) {
  auto* s = stream(owner, h);
  if (!s || !out || out->struct_size < sizeof(*out)) return T5_STREAM_INVALID;
  *out = {sizeof(*out), T5_STREAM_BYTES, s->flags, s->owner, s->capacity, s->used, s->high, s->terminal, s->read, s->written};
  return T5_STREAM_OK;
}
int32_t Registry::connect(uint32_t owner, t5_stream_t source, t5_stream_t dest, uint32_t policy, t5_pipe_t* out) {
  if (out) *out = 0;
  auto* s = stream(owner, source); auto* d = stream(owner, dest);
  if (!s || !d || !out || source == dest) return T5_STREAM_INVALID;
  if (policy != T5_PIPE_BLOCK_PRODUCER) return T5_STREAM_UNSUPPORTED;
  if (!(s->flags & T5_STREAM_READ) || !(d->flags & T5_STREAM_WRITE)) return T5_STREAM_DENIED;
  if (d->terminal) return T5_STREAM_CLOSED;
  if (leased(source, true) || leased(dest, false)) return T5_STREAM_BUSY;
  // One reader and writer per endpoint makes cycle detection a bounded walk.
  auto next = dest;
  for (unsigned i = 0; i <= MaxPipes; ++i) {
    if (next == source) return T5_STREAM_INVALID;
    bool found = false;
    for (auto& p : pipes_) if (p.owner && live(p.state) && p.source == next) { next = p.destination; found = true; break; }
    if (!found) break;
  }
  for (unsigned i = 0; i < MaxPipes; ++i) {
    auto& p = pipes_[i];
    if (p.owner || p.generation == MaxGeneration) continue;
    ++p.generation; p.owner = owner; p.state = T5_PIPE_RUNNING;
    p.source = source; p.destination = dest; p.used = p.offset = 0;
    p.error = 0; p.transferred = p.stalls = 0;
    *out = handle(p.generation, i); return T5_STREAM_OK;
  }
  return T5_STREAM_LIMIT;
}
int32_t Registry::pause(uint32_t owner, t5_pipe_t h, bool paused) {
  auto* p = pipe(owner, h); if (!p) return T5_STREAM_INVALID;
  if (!live(p->state)) return T5_STREAM_CLOSED;
  p->state = paused ? T5_PIPE_PAUSED : T5_PIPE_RUNNING; return T5_STREAM_OK;
}
int32_t Registry::cancel(uint32_t owner, t5_pipe_t h) {
  auto* p = pipe(owner, h); if (!p) return T5_STREAM_INVALID;
  if (live(p->state)) { p->state = T5_PIPE_CANCELLED; p->error = T5_STREAM_CANCELLED; p->used = p->offset = 0; }
  return T5_STREAM_OK;
}
int32_t Registry::closePipe(uint32_t owner, t5_pipe_t h) {
  auto* p = pipe(owner, h); if (!p) return T5_STREAM_INVALID;
  p->owner = 0; p->used = p->offset = 0; return T5_STREAM_OK;
}
int32_t Registry::pipeInfo(uint32_t owner, t5_pipe_t h, t5_pipe_info_t* out) {
  auto* p = pipe(owner, h);
  if (!p || !out || out->struct_size < sizeof(*out)) return T5_STREAM_INVALID;
  *out = {sizeof(*out), p->owner, p->state, p->used - p->offset, p->source, p->destination, p->error, p->transferred, p->stalls};
  return T5_STREAM_OK;
}
bool Registry::runnable() const {
  for (const auto& p : pipes_) if (p.owner && p.state == T5_PIPE_RUNNING) return true;
  return false;
}
void Registry::pump() {
  for (unsigned n = 0; n < MaxPipes; ++n) {
    auto& p = pipes_[(first_ + n) % MaxPipes];
    if (!p.owner || p.state != T5_PIPE_RUNNING) continue;
    auto* s = stream(p.owner, p.source); auto* d = stream(p.owner, p.destination);
    if (!s || !d) { p.state = T5_PIPE_FAILED; p.error = T5_STREAM_CLOSED; continue; }
    if (p.used == p.offset) {
      p.used = p.offset = 0;
      auto r = transfer(*s, true, p.data.data(), T5_STREAM_CHUNK, &p.used);
      if (r < 0) { p.state = T5_PIPE_FAILED; p.error = r; p.used = 0; continue; }
      if (r == T5_STREAM_EOF && !p.used) { p.state = T5_PIPE_DONE; continue; }
      if (!p.used) { ++p.stalls; continue; }
    }
    uint32_t written = 0;
    auto r = transfer(*d, false, p.data.data() + p.offset, p.used - p.offset, &written);
    p.offset += written; p.transferred += written;
    if (r < 0 || r == T5_STREAM_EOF) { p.state = T5_PIPE_FAILED; p.error = r; p.used = p.offset = 0; }
    else if (!written) ++p.stalls;
  }
  first_ = (first_ + 1) % MaxPipes;
}
void Registry::release(uint32_t owner) {
  for (auto& p : pipes_) if (p.owner == owner) { p.owner = 0; p.used = p.offset = 0; }
  for (unsigned i = 0; i < MaxStreams; ++i) if (streams_[i].owner == owner) close(owner, handle(streams_[i].generation, i));
}
}
