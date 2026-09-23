// The app-facing serial.port pair is distinct from the legacy open_usb slot.
// The bounded shuttle is transport-neutral and calls the selected semantic
// serial provider outside the stream mutex, preserving backpressure exactly.
namespace {
struct PendingSerialPair {
  t5_stream_t rx = 0, tx = 0;
  uint8_t rxBytes[T5_STREAM_CHUNK]{};
  uint8_t txBytes[T5_STREAM_CHUNK]{};
  uint32_t rxSize = 0, rxOffset = 0;
  uint32_t txSize = 0, txOffset = 0;
  bool rxInFlight = false, txInFlight = false;
  int32_t rxError = 0, txError = 0;
};
PendingSerialPair pendingSerial;

void failPairLocked(t5_stream_t handle, int32_t error) {
  if (!pairSerial || error >= 0) return;
  if (handle == pairRx || error == T5_STREAM_DISCONNECTED) {
    pendingSerial.rxError = error;
    if (pairRx) (void)registry.failEndpoint(pairSerial->owner, pairRx, error);
  }
  if (handle == pairTx || error == T5_STREAM_DISCONNECTED) {
    pendingSerial.txError = error;
    serialTxRetry.store(false, std::memory_order_release);
    if (pairTx) (void)registry.failEndpoint(pairSerial->owner, pairTx, error);
  }
}
int32_t flushPairRxLocked(t5_stream_t handle) {
  if (pendingSerial.rxOffset == pendingSerial.rxSize) return T5_STREAM_OK;
  const uint32_t remaining = pendingSerial.rxSize - pendingSerial.rxOffset;
  uint32_t accepted = 0;
  const auto result = registry.produce(pairSerial->owner, handle,
      pendingSerial.rxBytes + pendingSerial.rxOffset, remaining, &accepted);
  if (accepted > remaining) return T5_STREAM_IO;
  if (result != T5_STREAM_OK && result != T5_STREAM_AGAIN) return result;
  pendingSerial.rxOffset += accepted;
  if (pendingSerial.rxOffset == pendingSerial.rxSize)
    pendingSerial.rxOffset = pendingSerial.rxSize = 0;
  return T5_STREAM_OK;
}
} // namespace

int32_t pairTransport(t5_stream_t handle) {
  SerialLink snapshot{};
  {
    Lock lock;
    if (handle != pairRx && handle != pairTx) return T5_STREAM_OK;
    if (!pairSerial) return T5_STREAM_DISCONNECTED;
    const int32_t error = handle == pairRx ? pendingSerial.rxError : pendingSerial.txError;
    if (error) return error;
    snapshot = *pairSerial;  // Never dereference a session after dropping the lock.
  }
  return serialState(snapshot);
}
int32_t fillPairRx() {
  t5_stream_t handle;
  SerialLink snapshot{};
  uint32_t capacity = 0;
  {
    Lock lock;
    handle = pairRx;
    if (!pairSerial || !handle) return T5_STREAM_OK;
    if (pendingSerial.rxError) return pendingSerial.rxError;
    if (pendingSerial.rxInFlight) return T5_STREAM_AGAIN;
    snapshot = *pairSerial;
    if (pendingSerial.rxOffset < pendingSerial.rxSize)
      return flushPairRxLocked(handle);
    t5_stream_info_t info{}; info.struct_size = sizeof(info);
    const auto state = registry.info(snapshot.owner, handle, &info);
    if (state != T5_STREAM_OK) return state;
    capacity = std::min<uint32_t>(T5_STREAM_CHUNK, info.capacity - info.buffered);
    if (!capacity) return T5_STREAM_AGAIN;
    pendingSerial.rxInFlight = true;
  }
  uint8_t incoming[T5_STREAM_CHUNK]{};
  uint32_t received = 0;
  const auto result = serialReadProvider(&snapshot, incoming, capacity, &received);
  Lock lock;
  // An old completion cannot clear a replacement session's in-flight flag.
  if (!pairSerial || pairRx != handle || pairSerial->epoch != snapshot.epoch ||
      pendingSerial.rx != handle) return T5_STREAM_DISCONNECTED;
  pendingSerial.rxInFlight = false;
  if (result < 0 || received > capacity ||
      (result != T5_STREAM_OK && result != T5_STREAM_AGAIN)) {
    const int32_t error = result < 0 ? result : T5_STREAM_IO;
    failPairLocked(handle, error);
    return error;
  }
  if (!received) return T5_STREAM_AGAIN;
  std::memcpy(pendingSerial.rxBytes, incoming, received);
  pendingSerial.rxSize = received;
  pendingSerial.rxOffset = 0;
  const auto flushed = flushPairRxLocked(handle);
  if (flushed < 0) failPairLocked(handle, flushed);
  return flushed;
}
void serviceSerialPipeRx() {
  SerialLink snapshot{};
  t5_stream_t handle = 0;
  bool poll = false;
  {
    Lock lock;
    if (!pairSerial) return;
    snapshot = *pairSerial;
    handle = pairRx;
    poll = !pendingSerial.rxError && registry.pipeRunning(handle, true);
  }
  // Check revocation even with a saturated/paused RX queue. This performs no
  // physical I/O or discovery and never holds the stream lock over a provider.
  const auto state = serialState(snapshot);
  if (state < 0) {
    Lock lock;
    if (pairSerial && pairSerial->epoch == snapshot.epoch && pairRx == handle)
      failPairLocked(handle, state);
    return;
  }
  if (poll) (void)fillPairRx();
}
int32_t drainPairTx() {
  t5_stream_t handle;
  SerialLink snapshot{};
  uint8_t outgoing[T5_STREAM_CHUNK]{};
  uint32_t requested = 0;
  {
    Lock lock;
    handle = pairTx;
    if (!pairSerial || !handle) {
      serialTxRetry.store(false, std::memory_order_release);
      return T5_STREAM_OK;
    }
    if (pendingSerial.txError) return pendingSerial.txError;
    if (pendingSerial.txInFlight) {
      serialTxRetry.store(true, std::memory_order_release);
      return T5_STREAM_AGAIN;
    }
    snapshot = *pairSerial;
    if (pendingSerial.txOffset == pendingSerial.txSize) {
      uint32_t fetched = 0;
      const auto result = registry.consume(snapshot.owner, handle, pendingSerial.txBytes,
                                            sizeof(pendingSerial.txBytes), &fetched);
      if (result != T5_STREAM_OK && result != T5_STREAM_AGAIN) {
        serialTxRetry.store(false, std::memory_order_release);
        if (result < 0) failPairLocked(handle, result);
        return result;
      }
      if (fetched > sizeof(pendingSerial.txBytes)) {
        failPairLocked(handle, T5_STREAM_IO);
        return T5_STREAM_IO;
      }
      pendingSerial.txOffset = 0;
      pendingSerial.txSize = fetched;
      if (!fetched) {
        serialTxRetry.store(false, std::memory_order_release);
        return T5_STREAM_AGAIN;
      }
    }
    requested = pendingSerial.txSize - pendingSerial.txOffset;
    std::memcpy(outgoing, pendingSerial.txBytes + pendingSerial.txOffset, requested);
    pendingSerial.txInFlight = true;
    serialTxRetry.store(true, std::memory_order_release);
  }
  uint32_t written = 0;
  const auto result = serialWriteProvider(&snapshot, outgoing, requested, &written);
  Lock lock;
  if (!pairSerial || pairTx != handle || pairSerial->epoch != snapshot.epoch ||
      pendingSerial.tx != handle) return T5_STREAM_DISCONNECTED;
  pendingSerial.txInFlight = false;
  if (written > requested || (result != T5_STREAM_OK && result != T5_STREAM_AGAIN)) {
    // Keep the pending bytes for diagnosis/recovery, but do not spin on a
    // persistent physical error. Never silently treat a short write as done.
    const int32_t error = written > requested || result >= 0 ? T5_STREAM_IO : result;
    failPairLocked(handle, error);
    return error;
  }
  pendingSerial.txOffset += written;
  if (pendingSerial.txOffset == pendingSerial.txSize)
    pendingSerial.txOffset = pendingSerial.txSize = 0;
  // One additional bounded scheduler pass also drains bytes queued while the
  // previous provider write was in flight; an empty endpoint clears this flag.
  serialTxRetry.store(true, std::memory_order_release);
  return written ? T5_STREAM_OK : T5_STREAM_AGAIN;
}
#define SESSION_CALL(expr) do { if (!authorized()) return T5_STREAM_DENIED; Lock lock; auto r = (expr); wake(); return r; } while (0)
int32_t readStream(t5_stream_t h, void* d, uint32_t n, uint32_t* out) {
  if (out) *out = 0;
  if (!authorized()) return T5_STREAM_DENIED;
  if (h && (h == pairRx || h == pairTx)) {
    const auto transport = pairTransport(h);
    if (transport < 0) return transport;
  }
  if (h && h == pairRx) {
    const auto filled = fillPairRx();
    if (filled < 0) return filled;
    // Reading the response is also a bounded retry opportunity for a short
    // provider write. The pending TX buffer is never consumed twice.
    (void)drainPairTx();
  }
  RuntimeStreams::DirectIo operation{};
  int32_t result;
  {
    Lock lock;
    const auto guard = guardGnssStream(h);
    if (guard != T5_STREAM_OK) return guard;
    result = registry.read(owner, h, d, n, out, &operation);
  }
  if (operation.transfer.pending) result = executeDirect(operation, d, out);
  wake(); return result;
}

int32_t writeStream(t5_stream_t h, const void* d, uint32_t n, uint32_t* out) {
  if (out) *out = 0;
  if (!authorized()) return T5_STREAM_DENIED;
  if (h && (h == pairRx || h == pairTx)) {
    const auto transport = pairTransport(h);
    if (transport < 0) return transport;
  }
  RuntimeStreams::DirectIo operation{};
  int32_t result;
  {
    Lock lock;
    const auto guard = guardGnssStream(h);
    if (guard != T5_STREAM_OK) return guard;
    result = registry.write(owner, h, d, n, out, &operation);
  }
  if (operation.transfer.pending) result = executeDirect(operation, nullptr, out);
  if (result == T5_STREAM_OK && h && h == pairTx) (void)drainPairTx();
  wake(); return result;
}
int32_t finish(t5_stream_t h) {
  if (!authorized()) return T5_STREAM_DENIED;
  if (h && h == pairTx) {
    const auto state = pairTransport(h);
    if (state < 0) return state;
    const auto drained = drainPairTx();
    if (drained < 0) return drained;
    Lock lock;
    if (h != pairTx || !pairSerial) return T5_STREAM_DISCONNECTED;
    t5_stream_info_t info{}; info.struct_size = sizeof(info);
    const auto status = registry.info(owner, h, &info);
    if (status != T5_STREAM_OK) return status;
    if (pendingSerial.txInFlight || pendingSerial.txSize != pendingSerial.txOffset || info.buffered) {
      serialTxRetry.store(true, std::memory_order_release);
      wake(); return T5_STREAM_AGAIN;
    }
    // The drain check and EOF publication share one lock: a concurrent writer
    // cannot enqueue between them. Serial endpoints have no finish callback.
    const auto result = registry.finish(owner, h);
    wake(); return result;
  }
  RuntimeStreams::DirectIo operation{};
  int32_t result;
  { Lock lock; result = registry.finish(owner, h, T5_STREAM_EOF, &operation); }
  if (operation.transfer.pending) result = executeDirect(operation);
  wake(); return result;
}
int32_t seek(t5_stream_t h, uint64_t offset) {
  if (!authorized()) return T5_STREAM_DENIED;
  RuntimeStreams::DirectIo operation{};
  int32_t result;
  { Lock lock; result = registry.seek(owner, h, offset, &operation); }
  if (operation.transfer.pending) result = executeDirect(operation);
  wake(); return result;
}
int32_t closeStream(t5_stream_t h) {
  if (!authorized()) return T5_STREAM_DENIED;
  RetiredProvider retired;
  int32_t result;
  {
    Lock lock;
    result = registry.close(owner, h, &retired.value);
    if (result == T5_STREAM_OK) notePairClosed(h);
    forgetClosedGnssStream(h);
    (void)gnss.beforePoll(gnss.owner());
  }
  wake();
  return result;
}
int32_t info(t5_stream_t h, t5_stream_info_t* out) {
  if (!authorized()) return T5_STREAM_DENIED;
  Lock lock;
  const auto guard = guardGnssStream(h);
  if (guard != T5_STREAM_OK) return guard;
  const auto result = registry.info(owner, h, out);
  wake(); return result;
}
int32_t connect(t5_stream_t s, t5_stream_t d, uint32_t policy, t5_pipe_t* out) {
  if (out) *out = 0;
  if (!authorized()) return T5_STREAM_DENIED;
  Lock lock;
  if (guardGnssStream(s) != T5_STREAM_OK || guardGnssStream(d) != T5_STREAM_OK)
    return T5_STREAM_DENIED;
  if (gnssGrants.bound(owner, s) || gnssGrants.bound(owner, d)) return T5_STREAM_DENIED;
  const auto result = registry.connect(owner, s, d, policy, out);
  wake(); return result;
}
int32_t pause(t5_pipe_t h, uint32_t paused) { SESSION_CALL(registry.pause(owner, h, paused != 0)); }
int32_t cancel(t5_pipe_t h) { SESSION_CALL(registry.cancel(owner, h)); }
int32_t closePipe(t5_pipe_t h) { SESSION_CALL(registry.closePipe(owner, h)); }
int32_t pipeInfo(t5_pipe_t h, t5_pipe_info_t* out) { SESSION_CALL(registry.pipeInfo(owner, h, out)); }
#undef SESSION_CALL

int32_t openRecordBuffer(const char* schema, uint32_t maxRecord, uint32_t capacityRecords,
                         uint32_t flags, t5_stream_t* out) {
  if (out) *out = 0;
  if (!authorized()) return T5_STREAM_DENIED;
  Lock lock;
  const auto result = registry.recordBuffer(owner, schema, maxRecord, capacityRecords, out, flags);
  wake(); return result;
}
int32_t readRecord(t5_stream_t h, void* data, uint32_t capacity, uint32_t* size) {
  if (size) *size = 0;
  if (!authorized()) return T5_STREAM_DENIED;
  Lock lock;
  const auto guard = guardGnssStream(h);
  if (guard != T5_STREAM_OK) return guard;
  const auto result = registry.readRecord(owner, h, data, capacity, size);
  wake(); return result;
}
int32_t writeRecord(t5_stream_t h, const void* data, uint32_t size) {
  if (!authorized()) return T5_STREAM_DENIED;
  Lock lock;
  const auto guard = guardGnssStream(h);
  if (guard != T5_STREAM_OK) return guard;
  const auto result = registry.writeRecord(owner, h, data, size);
  wake(); return result;
}
int32_t recordInfo(t5_stream_t h, riscrte_record_info_v1* out) {
  if (!authorized()) return T5_STREAM_DENIED;
  if (!out || out->struct_size < sizeof(*out)) return T5_STREAM_INVALID;
  char schema[RISCRTE_RECORD_SCHEMA_CAPACITY]{};
  RuntimeStreams::RecordQueue::Stats stats{};
  t5_stream_info_t base{};
  base.struct_size = sizeof(base);
  Lock lock;
  const auto guard = guardGnssStream(h);
  if (guard != T5_STREAM_OK) return guard;
  auto result = registry.info(owner, h, &base);
  if (result != T5_STREAM_OK) return result;
  result = registry.recordInfo(owner, h, schema, sizeof(schema), &stats);
  if (result != T5_STREAM_OK) return result;
  riscrte_record_info_v1 value{};
  value.struct_size = sizeof(value);
  value.flags = base.flags;
  value.owner = base.owner;
  std::memcpy(value.schema, schema, sizeof(value.schema));
  value.max_record = stats.max_record;
  value.capacity_records = stats.capacity_records;
  value.queued_records = stats.queued_records;
  value.queued_bytes = stats.queued_bytes;
  value.high_water_records = stats.high_water_records;
  value.high_water_bytes = stats.high_water_bytes;
  value.terminal = stats.terminal;
  value.records_read = stats.records_read;
  value.records_written = stats.records_written;
  value.bytes_read = stats.bytes_read;
  value.bytes_written = stats.bytes_written;
  *out = value;
  return T5_STREAM_OK;
}
const t5_stream_api_v1 api = {T5_STREAM_API_VERSION, sizeof(t5_stream_api_v1), openBuffer, openFile, openUsb,
  openHttp, readStream, writeStream, finish, seek, closeStream, info, connect, pause, cancel, closePipe, pipeInfo};
const riscrte_stream_api_v2 api2 = {{RISCRTE_STREAM_API_VERSION_2, sizeof(riscrte_stream_api_v2),
  openBuffer, openFile, openUsb, openHttp, readStream, writeStream, finish, seek, closeStream,
  info, connect, pause, cancel, closePipe, pipeInfo},
  openRecordBuffer, readRecord, writeRecord, recordInfo};

void releaseStreams(void*, uint32_t id) {
  RetiredProviders retired;
  if (mutex) {
    Lock lock;
    gnss.releaseOwner(id);
    gnssGrants.releaseOwner(id);
    registry.release(id, &retired.values);
    notePairClosed(pairRx);
    notePairClosed(pairTx);
  }
  wake();
}
void releaseSerial(void*, uint32_t) { nativeSerialPortsEnd(); }
} // namespace
