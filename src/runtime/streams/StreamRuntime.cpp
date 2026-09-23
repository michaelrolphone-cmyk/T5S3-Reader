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
Registry::Stream* Registry::streamByHandle(t5_stream_t h) {
  unsigned i = (h & 255) - 1;
  if (i >= MaxStreams) return nullptr;
  auto& s = streams_[i];
  return s.owner && s.generation == (h >> 8) ? &s : nullptr;
}
Registry::Stream* Registry::accessible(uint32_t caller, t5_stream_t h) {
  auto* s = streamByHandle(h);
  if (!s || !caller || s->closing) return nullptr;
  if (s->owner == caller) return s;
  for (const auto& g : s->grants) if (g.consumer == caller) return s;
  return nullptr;
}
void Registry::invalidateUnauthorizedPipes() {
  for (auto& p : pipes_) {
    if (!p.owner || !live(p.state)) continue;
    auto* s = streamByHandle(p.source);
    auto* d = streamByHandle(p.destination);
    if (s && d && allowed(*s, p.owner, T5_STREAM_READ) &&
        allowed(*d, p.owner, T5_STREAM_WRITE)) continue;
    p.state = T5_PIPE_FAILED; p.error = T5_STREAM_DENIED;
    p.used = p.offset = 0; p.recordPending = false;
  }
}
bool Registry::allowed(const Stream& s, uint32_t caller, uint32_t need) const {
  if (!caller || !need || s.closing) return false;
  if (s.owner == caller) return (s.flags & need) == need;
  for (const auto& g : s.grants) {
    if (g.consumer == caller && (g.rights & need) == need) return true;
  }
  return false;
}
void Registry::failPipes(t5_stream_t h, int32_t error) {
  for (auto& p : pipes_) if (p.owner && live(p.state) && (p.source == h || p.destination == h)) {
    p.state = T5_PIPE_FAILED; p.error = error; p.used = p.offset = 0; p.recordPending = false;
  }
}
void Registry::clearGrants(Stream& s) {
  for (auto& g : s.grants) g = {};
}
bool Registry::usesExternalProvider(const Stream& s) const {
  return s.kind == T5_STREAM_BYTES && !s.buffer && !s.elfEndpoint &&
         (s.provider.read || s.provider.write);
}
int32_t Registry::attach(uint32_t owner, uint32_t kind, uint32_t flags, Provider provider, t5_stream_t* out) {
  if (out) *out = 0;
  if (!out || !owner || !(flags & 3) || (flags & ~7u)) return T5_STREAM_INVALID;
  if (kind != T5_STREAM_BYTES) return T5_STREAM_UNSUPPORTED;
  for (unsigned i = 0; i < MaxStreams; ++i) {
    auto& s = streams_[i];
    if (s.owner || s.generation == MaxGeneration) continue;
    ++s.generation; s.owner = owner; s.flags = flags; s.kind = kind; s.provider = provider;
    s.records.reset();
    s.terminal = 0; s.capacity = s.head = s.used = s.high = 0; s.read = s.written = 0;
    s.protection = kStreamPublic; s.elfEndpoint = false; s.closing = false; s.inFlight = 0; clearGrants(s);
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
int32_t Registry::recordBuffer(uint32_t owner, const char* schema, uint32_t maxRecord,
                               uint32_t capacityRecords, t5_stream_t* out, uint32_t flags) {
  if (out) *out = 0;
  if (!out || !(flags & 3) || (flags & ~3u) ||
      !RecordQueue::validSchema(schema) || !maxRecord || maxRecord > RecordQueue::MaxRecord ||
      !capacityRecords || capacityRecords > RecordQueue::MaxQueued ||
      capacityRecords > RecordQueue::MaxStorage / maxRecord) return T5_STREAM_INVALID;
  auto result = attach(owner, T5_STREAM_BYTES, flags, {}, out);
  if (result != T5_STREAM_OK) return result;
  auto* s = stream(owner, *out);
  result = s->records.configure(schema, maxRecord, capacityRecords);
  if (result != T5_STREAM_OK) { close(owner, *out); *out = 0; return result; }
  s->kind = T5_STREAM_RECORDS;
  return T5_STREAM_OK;
}
int32_t Registry::readRecord(uint32_t owner, t5_stream_t h, void* data, uint32_t capacity, uint32_t* size) {
  if (size) *size = 0;
  auto* s = accessible(owner, h);
  if (!s || !size || (!data && capacity)) return T5_STREAM_INVALID;
  if (s->kind != T5_STREAM_RECORDS) return T5_STREAM_UNSUPPORTED;
  if (!allowed(*s, owner, T5_STREAM_READ)) return T5_STREAM_DENIED;
  if (leased(h, true)) return T5_STREAM_BUSY;
  const auto result = s->records.read(data, capacity, size);
  if (result == T5_STREAM_OK) s->read += *size;
  return result;
}
int32_t Registry::writeRecord(uint32_t owner, t5_stream_t h, const void* data, uint32_t size) {
  auto* s = accessible(owner, h);
  if (!s) return T5_STREAM_INVALID;
  if (s->kind != T5_STREAM_RECORDS) return T5_STREAM_UNSUPPORTED;
  if (!allowed(*s, owner, T5_STREAM_WRITE)) return T5_STREAM_DENIED;
  if (leased(h, false)) return T5_STREAM_BUSY;
  const auto result = s->records.write(data, size);
  if (result == T5_STREAM_OK) s->written += size;
  return result;
}
int32_t Registry::recordInfo(uint32_t owner, t5_stream_t h, char* schema,
                             uint32_t schemaCapacity, RecordQueue::Stats* out) {
  auto* s = accessible(owner, h);
  if (!s || !schema || !out) return T5_STREAM_INVALID;
  if (s->kind != T5_STREAM_RECORDS) return T5_STREAM_UNSUPPORTED;
  const size_t length = std::strlen(s->records.schema()) + 1;
  if (schemaCapacity < length) return T5_STREAM_LIMIT;
  std::memcpy(schema, s->records.schema(), length);
  *out = s->records.stats();
  return T5_STREAM_OK;
}
int32_t Registry::produce(uint32_t owner, t5_stream_t h, const void* data, uint32_t size, uint32_t* count) {
  if (count) *count = 0;
  auto* s = stream(owner, h);
  if (!s || !count || (!data && size)) return T5_STREAM_INVALID;
  if (s->kind != T5_STREAM_BYTES || !s->buffer) return T5_STREAM_UNSUPPORTED;
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
  if (s.kind != T5_STREAM_BYTES) return T5_STREAM_UNSUPPORTED;
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

// Generation alone does not grant rights. A present, unauthorized handle is
// INVALID; a recognized grantee/owner missing the requested right is DENIED.
int32_t Registry::read(uint32_t caller, t5_stream_t h, void* data, uint32_t size, uint32_t* count, DirectIo* io) {
  if (io) *io = {};
  if (count) *count = 0;
  auto* s = accessible(caller, h);
  if (!s || !count || (!data && size)) return T5_STREAM_INVALID;
  if (!allowed(*s, caller, T5_STREAM_READ)) return T5_STREAM_DENIED;
  if (s->kind != T5_STREAM_BYTES) return T5_STREAM_UNSUPPORTED;
  if (leased(h, true)) return T5_STREAM_BUSY;
  if (s->inFlight) return T5_STREAM_BUSY;
  if (io && usesExternalProvider(*s) && size && !s->terminal) {
    auto result = prepareDirect(*s, h, caller, DirectIo::Operation::Read, *io);
    if (result == T5_STREAM_OK) io->transfer.request = std::min(size, T5_STREAM_CHUNK);
    return result;
  }
  return transfer(*s, true, data, size, count);
}
int32_t Registry::write(uint32_t caller, t5_stream_t h, const void* data, uint32_t size, uint32_t* count, DirectIo* io) {
  if (io) *io = {};
  if (count) *count = 0;
  auto* s = accessible(caller, h);
  if (!s || !count || (!data && size)) return T5_STREAM_INVALID;
  if (!allowed(*s, caller, T5_STREAM_WRITE)) return T5_STREAM_DENIED;
  if (s->kind != T5_STREAM_BYTES) return T5_STREAM_UNSUPPORTED;
  if (leased(h, false)) return T5_STREAM_BUSY;
  if (s->inFlight) return T5_STREAM_BUSY;
  if (io && usesExternalProvider(*s) && size && !s->terminal) {
    auto result = prepareDirect(*s, h, caller, DirectIo::Operation::Write, *io);
    if (result == T5_STREAM_OK) {
      io->transfer.request = std::min(size, T5_STREAM_CHUNK);
      std::memcpy(io->transfer.payload.data(), data, io->transfer.request);
    }
    return result;
  }
  return transfer(*s, false, const_cast<void*>(data), size, count);
}
int32_t Registry::finish(uint32_t owner, t5_stream_t h, int32_t terminal, DirectIo* io) {
  if (io) *io = {};
  auto* s = stream(owner, h);
  if (!s || (terminal != T5_STREAM_EOF && terminal >= 0)) return T5_STREAM_INVALID;
  if (s->closing || s->inFlight || leased(h, false)) return T5_STREAM_BUSY;
  if (s->terminal) return s->terminal == T5_STREAM_EOF ? T5_STREAM_OK : s->terminal;
  if (s->kind == T5_STREAM_RECORDS) {
    auto result = s->records.finish(terminal);
    if (result == T5_STREAM_OK) s->terminal = terminal;
    return result;
  }
  if (s->provider.finish && terminal == T5_STREAM_EOF) {
    if (io) return prepareDirect(*s, h, owner, DirectIo::Operation::Finish, *io);
    auto r = s->provider.finish(s->provider.context); if (r != T5_STREAM_OK) { if (r < 0) s->terminal = r; return r; }
  }
  s->terminal = terminal; return T5_STREAM_OK;
}
int32_t Registry::seek(uint32_t owner, t5_stream_t h, uint64_t offset, DirectIo* io) {
  if (io) *io = {};
  auto* s = stream(owner, h);
  if (!s) return T5_STREAM_INVALID;
  if (s->kind != T5_STREAM_BYTES) return T5_STREAM_UNSUPPORTED;
  if (s->closing || s->inFlight || leased(h, true) || leased(h, false)) return T5_STREAM_BUSY;
  if (!(s->flags & T5_STREAM_SEEK) || !s->provider.seek) return T5_STREAM_UNSUPPORTED;
  if (io) {
    const auto result = prepareDirect(*s, h, owner, DirectIo::Operation::Seek, *io);
    io->offset = offset;
    return result;
  }
  auto r = s->provider.seek(s->provider.context, offset);
  if (r == T5_STREAM_OK) s->terminal = 0;
  return r;
}
int32_t Registry::prepareDirect(Stream& s, t5_stream_t h, uint32_t caller,
                                  DirectIo::Operation operation, DirectIo& io) {
  if (nextTicket_ == UINT64_MAX) return T5_STREAM_LIMIT;
  io.operation = operation;
  io.caller = caller;
  if (s.owner != caller)
    for (const auto& grant : s.grants)
      if (grant.consumer == caller) { io.grantTicket = grant.ticket; break; }
  io.transfer.pending = true;
  io.transfer.reading = operation == DirectIo::Operation::Read;
  io.transfer.streamIndex = (h & 255) - 1;
  io.transfer.streamGeneration = s.generation;
  io.transfer.provider = s.provider;
  io.transfer.ticket = s.inFlight = ++nextTicket_;
  return T5_STREAM_OK;
}
int32_t Registry::directComplete(const DirectIo& operation, int32_t result, uint32_t count,
                                  void* output, uint32_t* actual, Provider* retired) {
  if (actual) *actual = 0;
  if (retired) *retired = {};
  const auto& io = operation.transfer;
  if (!io.pending || io.streamIndex >= MaxStreams) return T5_STREAM_INVALID;
  auto& s = streams_[io.streamIndex];
  if (!s.owner || s.generation != io.streamGeneration || !io.ticket ||
      s.inFlight != io.ticket) return T5_STREAM_CLOSED;
  s.inFlight = 0;
  if (s.closing) {
    (void)close(s.owner, handle(s.generation, io.streamIndex), retired);
    return T5_STREAM_CLOSED;
  }
  using Op = DirectIo::Operation;
  const auto op = operation.operation;
  if (op == Op::Read || op == Op::Write) {
    bool sameGrant = s.owner == operation.caller;
    for (const auto& grant : s.grants)
      if (grant.consumer == operation.caller && operation.grantTicket &&
          grant.ticket == operation.grantTicket) sameGrant = true;
    if (!sameGrant || !allowed(s, operation.caller, op == Op::Read ? T5_STREAM_READ : T5_STREAM_WRITE))
      return T5_STREAM_DENIED;
    if (count > io.request || count > io.payload.size() || !actual ||
        (op == Op::Read && count && !output)) {
      s.terminal = T5_STREAM_IO;
      return T5_STREAM_IO;
    }
    if (result < 0 || result == T5_STREAM_EOF) s.terminal = result;
    if (op == Op::Read) {
      if (count) std::memcpy(output, io.payload.data(), count);
      s.read += count;
    } else s.written += count;
    *actual = count;
  } else {
    if (s.owner != operation.caller) return T5_STREAM_DENIED;
    if (op == Op::Seek && result == T5_STREAM_OK) s.terminal = 0;
    if (op == Op::Finish) {
      // AGAIN means flush is incomplete, not EOF or a sticky terminal state.
      if (result == T5_STREAM_OK) s.terminal = T5_STREAM_EOF;
      else if (result < 0) s.terminal = result;
    }
  }
  return result;
}
int32_t Registry::close(uint32_t owner, t5_stream_t h, Provider* retired) {
  if (retired) *retired = {};
  auto* s = stream(owner, h);
  if (!s) return T5_STREAM_INVALID;
  failPipes(h, T5_STREAM_CLOSED);
  // A prepared operation owns the adapter context until its exact completion.
  // Owner exit may request close, but cannot destroy it beneath provider I/O.
  s->closing = true;
  clearGrants(*s);
  if (s->inFlight) return T5_STREAM_BUSY;
  if (retired) *retired = s->provider;
  else if (s->provider.close) s->provider.close(s->provider.context);
  s->buffer.reset(); s->records.reset(); s->provider = {};
  clearGrants(*s);
  s->elfEndpoint = false; s->protection = kStreamPublic;
  s->owner = 0;
  return T5_STREAM_OK;
}
int32_t Registry::info(uint32_t owner, t5_stream_t h, t5_stream_info_t* out) {
  auto* s = accessible(owner, h);
  if (!s || !out || out->struct_size < sizeof(*out)) return T5_STREAM_INVALID;
  if (s->kind == T5_STREAM_RECORDS) {
    const auto r = s->records.stats();
    *out = {sizeof(*out), s->kind, s->flags, s->owner, r.capacity_records * r.max_record,
            r.queued_bytes, r.high_water_bytes, r.terminal, r.bytes_read, r.bytes_written};
  } else {
    *out = {sizeof(*out), s->kind, s->flags, s->owner, s->capacity, s->used,
            s->high, s->terminal, s->read, s->written};
  }
  return T5_STREAM_OK;
}
int32_t Registry::connectInternal(uint32_t pipeOwner, Stream* s, t5_stream_t source,
                                  Stream* d, t5_stream_t dest, uint32_t policy, t5_pipe_t* out) {
  if (out) *out = 0;
  if (!s || !d || !out || !pipeOwner || source == dest) return T5_STREAM_INVALID;
  if (policy != T5_PIPE_BLOCK_PRODUCER) return T5_STREAM_UNSUPPORTED;
  if (!(s->flags & T5_STREAM_READ) || !(d->flags & T5_STREAM_WRITE)) return T5_STREAM_DENIED;
  if (s->kind != d->kind || (s->kind == T5_STREAM_RECORDS &&
      !RecordQueue::compatible(s->records.schema(), d->records.schema()))) return T5_STREAM_UNSUPPORTED;
  if (s->kind == T5_STREAM_RECORDS && s->records.maxRecord() > d->records.maxRecord())
    return T5_STREAM_LIMIT;
  // A destination's self-declared protection flag is not a retention/purge
  // policy. Reject protected copies until downstream authority is implemented.
  if (s->protection == kStreamProtected) return T5_STREAM_DENIED;
  if (s->closing || d->closing || s->inFlight || d->inFlight) return T5_STREAM_BUSY;
  if (d->terminal) return T5_STREAM_CLOSED;
  if (leased(source, true) || leased(dest, false)) return T5_STREAM_BUSY;
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
    ++p.generation; p.owner = pipeOwner; p.state = T5_PIPE_RUNNING;
    p.source = source; p.destination = dest; p.used = p.offset = 0; p.recordPending = false;
    p.error = 0; p.transferred = p.stalls = 0;
    *out = handle(p.generation, i); return T5_STREAM_OK;
  }
  return T5_STREAM_LIMIT;
}
int32_t Registry::connect(uint32_t owner, t5_stream_t source, t5_stream_t dest, uint32_t policy, t5_pipe_t* out) {
  if (out) *out = 0;
  auto* s = accessible(owner, source);
  auto* d = accessible(owner, dest);
  if (!s || !d) return T5_STREAM_INVALID;
  if (!allowed(*s, owner, T5_STREAM_READ) || !allowed(*d, owner, T5_STREAM_WRITE))
    return T5_STREAM_DENIED;
  return connectInternal(owner, s, source, d, dest, policy, out);
}
int32_t Registry::publishEndpoint(uint32_t publisher, uint32_t kind, uint32_t flags,
                                  uint32_t byteCapacity, const char* schema,
                                  uint32_t maxRecord, uint32_t capacityRecords,
                                  uint32_t protection, t5_stream_t* out) {
  if (out) *out = 0;
  if (!publisher || !out) return T5_STREAM_INVALID;
  if (protection != kStreamPublic && protection != kStreamProtected) return T5_STREAM_INVALID;
  int32_t result;
  if (kind == T5_STREAM_BYTES) {
    result = buffer(publisher, byteCapacity ? byteCapacity : T5_STREAM_CHUNK, out, flags);
  } else if (kind == T5_STREAM_RECORDS) {
    result = recordBuffer(publisher, schema, maxRecord, capacityRecords, out, flags);
  } else {
    return T5_STREAM_UNSUPPORTED;
  }
  if (result != T5_STREAM_OK) return result;
  auto* s = stream(publisher, *out);
  s->elfEndpoint = true;
  s->protection = protection;
  s->provider = {};
  return T5_STREAM_OK;
}
int32_t Registry::grant(uint32_t publisher, t5_stream_t h, uint32_t consumer, uint32_t rights) {
  auto* s = stream(publisher, h);
  if (!s || !consumer || !rights || (rights & ~7u) || consumer == publisher) return T5_STREAM_INVALID;
  if (s->closing) return T5_STREAM_CLOSED;
  if ((s->flags & rights) != rights) return T5_STREAM_DENIED;
  int free = -1;
  for (unsigned i = 0; i < MaxGrants; ++i) {
    if (s->grants[i].consumer == consumer) {
      if (s->grants[i].rights == rights) return T5_STREAM_OK;
      if (nextTicket_ == UINT64_MAX) return T5_STREAM_LIMIT;
      s->grants[i].rights = rights;
      s->grants[i].ticket = ++nextTicket_;
      invalidateUnauthorizedPipes();
      return T5_STREAM_OK;
    }
    if (free < 0 && !s->grants[i].consumer) free = static_cast<int>(i);
  }
  if (free < 0) return T5_STREAM_LIMIT;
  if (nextTicket_ == UINT64_MAX) return T5_STREAM_LIMIT;
  s->grants[free] = {consumer, rights, ++nextTicket_};
  return T5_STREAM_OK;
}
int32_t Registry::revoke(uint32_t publisher, t5_stream_t h, uint32_t consumer) {
  auto* s = stream(publisher, h);
  if (!s) return T5_STREAM_INVALID;
  if (!consumer) {
    failPipes(h, T5_STREAM_DISCONNECTED);
    clearGrants(*s);
    return close(publisher, h);
  }
  bool found = false;
  for (auto& g : s->grants) {
    if (g.consumer != consumer) continue;
    g = {};
    found = true;
  }
  if (!found) return T5_STREAM_INVALID;
  for (auto& p : pipes_) {
    if (!p.owner || p.owner != consumer || !live(p.state)) continue;
    if (p.source == h || p.destination == h) {
      p.state = T5_PIPE_FAILED; p.error = T5_STREAM_DISCONNECTED;
      p.used = p.offset = 0; p.recordPending = false;
    }
  }
  return T5_STREAM_OK;
}
int32_t Registry::connectAcross(uint32_t pipeOwner, uint32_t sourceOwner, t5_stream_t source,
                                uint32_t destOwner, t5_stream_t dest, uint32_t policy, t5_pipe_t* out) {
  if (out) *out = 0;
  auto* s = stream(sourceOwner, source);
  auto* d = stream(destOwner, dest);
  if (!s || !d) return T5_STREAM_INVALID;
  if (!allowed(*s, pipeOwner, T5_STREAM_READ) || !allowed(*d, pipeOwner, T5_STREAM_WRITE))
    return T5_STREAM_DENIED;
  return connectInternal(pipeOwner, s, source, d, dest, policy, out);
}
int32_t Registry::pause(uint32_t owner, t5_pipe_t h, bool paused) {
  auto* p = pipe(owner, h); if (!p) return T5_STREAM_INVALID;
  if (!live(p->state)) return T5_STREAM_CLOSED;
  p->state = paused ? T5_PIPE_PAUSED : T5_PIPE_RUNNING; return T5_STREAM_OK;
}
int32_t Registry::cancel(uint32_t owner, t5_pipe_t h) {
  auto* p = pipe(owner, h); if (!p) return T5_STREAM_INVALID;
  if (live(p->state)) {
    p->state = T5_PIPE_CANCELLED; p->error = T5_STREAM_CANCELLED;
    p->used = p->offset = 0; p->recordPending = false;
  }
  return T5_STREAM_OK;
}
int32_t Registry::closePipe(uint32_t owner, t5_pipe_t h) {
  auto* p = pipe(owner, h); if (!p) return T5_STREAM_INVALID;
  p->owner = 0; p->used = p->offset = 0; p->recordPending = false; return T5_STREAM_OK;
}
int32_t Registry::pipeInfo(uint32_t owner, t5_pipe_t h, t5_pipe_info_t* out) {
  auto* p = pipe(owner, h);
  if (!p || !out || out->struct_size < sizeof(*out)) return T5_STREAM_INVALID;
  *out = {sizeof(*out), p->owner, p->state, p->used - p->offset, p->source, p->destination,
          p->error, p->transferred, p->stalls};
  return T5_STREAM_OK;
}
bool Registry::runnable() const {
  for (const auto& p : pipes_) if (p.owner && p.state == T5_PIPE_RUNNING) return true;
  return false;
}
Registry::Step Registry::pumpIndex(unsigned index, ExternalIo* io) {
  auto& p = pipes_[index];
  if (!p.owner || p.state != T5_PIPE_RUNNING) return Step::Idle;
  auto* s = streamByHandle(p.source); auto* d = streamByHandle(p.destination);
  if (!s || !d) { p.state = T5_PIPE_FAILED; p.error = T5_STREAM_CLOSED; return Step::Progressed; }
  if (!allowed(*s, p.owner, T5_STREAM_READ) || !allowed(*d, p.owner, T5_STREAM_WRITE)) {
    p.state = T5_PIPE_FAILED; p.error = T5_STREAM_DENIED;
    p.used = p.offset = 0; p.recordPending = false;
    return Step::Progressed;
  }
  if (s->inFlight || d->inFlight) return Step::Idle;
  if (nextTicket_ == UINT64_MAX && (usesExternalProvider(*s) || usesExternalProvider(*d))) {
    p.state = T5_PIPE_FAILED; p.error = T5_STREAM_LIMIT;
    p.used = p.offset = 0; p.recordPending = false;
    return Step::Progressed;
  }
  if (s->kind == T5_STREAM_RECORDS) {
    if (!p.recordPending) {
      p.used = p.offset = 0;
      const auto result = s->records.read(p.data.data(), p.data.size(), &p.used);
      if (result == T5_STREAM_EOF) { p.state = T5_PIPE_DONE; return Step::Progressed; }
      if (result == T5_STREAM_AGAIN) { ++p.stalls; return Step::Progressed; }
      if (result != T5_STREAM_OK) {
        p.state = T5_PIPE_FAILED; p.error = result; p.used = 0; return Step::Progressed;
      }
      p.recordPending = true;
    }
    const auto result = d->records.write(p.data.data(), p.used);
    if (result == T5_STREAM_AGAIN) { ++p.stalls; return Step::Progressed; }
    if (result != T5_STREAM_OK) {
      p.state = T5_PIPE_FAILED; p.error = result; p.used = p.offset = 0; p.recordPending = false;
      return Step::Progressed;
    }
    p.transferred += p.used;
    p.used = p.offset = 0; p.recordPending = false;
    return Step::Progressed;
  }
  if (p.used == p.offset) {
    if (usesExternalProvider(*s) && !s->terminal && io) {
      io->pending = true; io->reading = true;
      io->pipeIndex = index; io->pipeGeneration = p.generation;
      io->streamIndex = static_cast<unsigned>((p.source & 255) - 1);
      io->streamGeneration = s->generation;
      io->provider = s->provider;
      io->request = T5_STREAM_CHUNK;
      io->ticket = s->inFlight = ++nextTicket_;
      return Step::NeedIo;
    }
    p.used = p.offset = 0;
    auto r = transfer(*s, true, p.data.data(), T5_STREAM_CHUNK, &p.used);
    if (r < 0) { p.state = T5_PIPE_FAILED; p.error = r; p.used = 0; return Step::Progressed; }
    if (r == T5_STREAM_EOF && !p.used) { p.state = T5_PIPE_DONE; return Step::Progressed; }
    if (!p.used) { ++p.stalls; return Step::Progressed; }
  }
  if (usesExternalProvider(*d) && io) {
    io->pending = true; io->reading = false;
    io->pipeIndex = index; io->pipeGeneration = p.generation;
    io->streamIndex = static_cast<unsigned>((p.destination & 255) - 1);
    io->streamGeneration = d->generation;
    io->provider = d->provider;
    io->request = p.used - p.offset;
    io->ticket = d->inFlight = ++nextTicket_;
    if (io->request) std::memcpy(io->payload.data(), p.data.data() + p.offset, io->request);
    return Step::NeedIo;
  }
  uint32_t written = 0;
  auto r = transfer(*d, false, p.data.data() + p.offset, p.used - p.offset, &written);
  p.offset += written; p.transferred += written;
  if (r < 0 || r == T5_STREAM_EOF) { p.state = T5_PIPE_FAILED; p.error = r; p.used = p.offset = 0; }
  else if (!written) ++p.stalls;
  return Step::Progressed;
}
void Registry::pump() {
  for (unsigned n = 0; n < MaxPipes; ++n) {
    ExternalIo io{};
    auto step = pumpIndex((first_ + n) % MaxPipes, &io);
    if (step != Step::NeedIo) continue;
    uint8_t scratch[T5_STREAM_CHUNK];
    uint32_t count = 0;
    int32_t result;
    if (io.reading) {
      result = io.provider.read ? io.provider.read(io.provider.context, scratch, io.request, &count)
                                : T5_STREAM_UNSUPPORTED;
      pumpComplete(io, result, scratch, count);
    } else {
      result = io.provider.write
                   ? io.provider.write(io.provider.context, io.payload.data(), io.request, &count)
                   : T5_STREAM_UNSUPPORTED;
      pumpComplete(io, result, nullptr, count);
    }
  }
  first_ = (first_ + 1) % MaxPipes;
}
bool Registry::pumpPrepare(ExternalIo* io) {
  if (!io) return false;
  *io = {};
  for (unsigned n = 0; n < MaxPipes; ++n) {
    unsigned index = (first_ + n) % MaxPipes;
    auto step = pumpIndex(index, io);
    if (step == Step::NeedIo) {
      first_ = (index + 1) % MaxPipes;
      return true;
    }
  }
  first_ = (first_ + 1) % MaxPipes;
  return false;
}
void Registry::pumpComplete(const ExternalIo& io, int32_t result, const void* data, uint32_t count, Provider* retired) {
  if (retired) *retired = {};
  if (!io.pending || io.pipeIndex >= MaxPipes || io.streamIndex >= MaxStreams) return;
  auto& p = pipes_[io.pipeIndex];
  auto& s = streams_[io.streamIndex];
  if (!s.owner || s.generation != io.streamGeneration || !io.ticket || s.inFlight != io.ticket) return;
  s.inFlight = 0;
  if (s.closing) {
    (void)close(s.owner, handle(s.generation, io.streamIndex), retired);
    return;
  }
  // Pausing prevents the next dispatch; it must not discard bytes already
  // consumed by the outstanding read or replay an accepted partial write.
  if (!p.owner || p.generation != io.pipeGeneration || !live(p.state)) return;
  if (count > io.request || (io.reading && count && !data)) { count = 0; result = T5_STREAM_IO; }
  if (io.reading) {
    if (result < 0) { p.state = T5_PIPE_FAILED; p.error = result; p.used = p.offset = 0; s.terminal = result; return; }
    if (result == T5_STREAM_EOF && !count) { p.state = T5_PIPE_DONE; s.terminal = T5_STREAM_EOF; return; }
    if (!count) { ++p.stalls; if (result == T5_STREAM_EOF) s.terminal = T5_STREAM_EOF; return; }
    p.used = 0; p.offset = 0;
    uint32_t n = count < T5_STREAM_CHUNK ? count : T5_STREAM_CHUNK;
    if (data) std::memcpy(p.data.data(), data, n);
    p.used = n;
    s.read += n;
    if (result < 0 || result == T5_STREAM_EOF) s.terminal = result;
  } else {
    if (result < 0 || result == T5_STREAM_EOF) {
      p.state = T5_PIPE_FAILED; p.error = result == T5_STREAM_EOF ? T5_STREAM_EOF : result;
      p.used = p.offset = 0; s.terminal = result; return;
    }
    p.offset += count; p.transferred += count; s.written += count;
    if (!count) ++p.stalls;
  }
}
void Registry::release(uint32_t owner, std::array<Provider, MaxStreams>* retired) {
  if (retired) *retired = {};
  for (auto& p : pipes_) if (p.owner == owner) {
    p.owner = 0; p.used = p.offset = 0; p.recordPending = false;
  }
  for (auto& s : streams_) {
    if (!s.owner) continue;
    for (auto& g : s.grants) if (g.consumer == owner) g = {};
  }
  for (unsigned i = 0; i < MaxStreams; ++i)
    if (streams_[i].owner == owner) close(owner, handle(streams_[i].generation, i), retired ? &(*retired)[i] : nullptr);
}
}
