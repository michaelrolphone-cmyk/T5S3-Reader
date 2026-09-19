// The app-facing serial.port pair is distinct from the legacy open_usb slot.
// These shuttles perform class ELF I/O without holding the stream mutex and
// preserve a single bounded pending chunk in each direction on backpressure.
namespace {
struct PendingUsbPair {
  t5_stream_t rx = 0, tx = 0;
  uint8_t rxBytes[T5_STREAM_CHUNK]{};
  uint8_t txBytes[T5_STREAM_CHUNK]{};
  uint32_t rxSize = 0, rxOffset = 0;
  uint32_t txSize = 0, txOffset = 0;
  bool txInFlight = false;
};
PendingUsbPair pendingUsb;

int32_t flushPairRxLocked(t5_stream_t handle) {
  if (pendingUsb.rxOffset == pendingUsb.rxSize) return T5_STREAM_OK;
  const uint32_t remaining = pendingUsb.rxSize - pendingUsb.rxOffset;
  uint32_t accepted = 0;
  const auto result = registry.produce(owner, handle,
      pendingUsb.rxBytes + pendingUsb.rxOffset, remaining, &accepted);
  if (accepted > remaining) return T5_STREAM_IO;
  if (result != T5_STREAM_OK && result != T5_STREAM_AGAIN) return result;
  pendingUsb.rxOffset += accepted;
  if (pendingUsb.rxOffset == pendingUsb.rxSize)
    pendingUsb.rxOffset = pendingUsb.rxSize = 0;
  return T5_STREAM_OK;
}
} // namespace

int32_t pairTransport(t5_stream_t handle) {
  Usb snapshot{};
  {
    Lock lock;
    if (handle != pairRx && handle != pairTx) return T5_STREAM_OK;
    if (!pairUsb) return T5_STREAM_DISCONNECTED;
    snapshot = *pairUsb;  // Never dereference a session after dropping the lock.
  }
  return usbState(snapshot);
}
int32_t fillPairRx() {
  t5_stream_t handle;
  Usb snapshot{};
  {
    Lock lock;
    handle = pairRx;
    if (!pairUsb || !handle) return T5_STREAM_OK;
    snapshot = *pairUsb;
    if (pendingUsb.rx != handle) {
      pendingUsb.rx = handle;
      pendingUsb.rxSize = pendingUsb.rxOffset = 0;
    }
    // Do not poll the class again until every previously read byte fits in
    // the bounded RX endpoint; the producer must honor downstream pressure.
    if (pendingUsb.rxOffset < pendingUsb.rxSize)
      return flushPairRxLocked(handle);
  }
  uint8_t incoming[T5_STREAM_CHUNK]{};
  uint32_t received = 0;
  const auto result = usbRead(&snapshot, incoming, sizeof(incoming), &received);
  if (result < 0 || !received) return result;
  if (received > sizeof(incoming)) return T5_STREAM_IO;
  Lock lock;
  if (!pairUsb || pairRx != handle || pairUsb->epoch != snapshot.epoch ||
      pendingUsb.rx != handle) return T5_STREAM_DISCONNECTED;
  std::memcpy(pendingUsb.rxBytes, incoming, received);
  pendingUsb.rxSize = received;
  pendingUsb.rxOffset = 0;
  return flushPairRxLocked(handle);
}
int32_t drainPairTx() {
  t5_stream_t handle;
  Usb snapshot{};
  uint8_t outgoing[T5_STREAM_CHUNK]{};
  uint32_t requested = 0;
  {
    Lock lock;
    handle = pairTx;
    if (!pairUsb || !handle) {
      usbTxRetry.store(false, std::memory_order_release);
      return T5_STREAM_OK;
    }
    if (pendingUsb.txInFlight) {
      usbTxRetry.store(true, std::memory_order_release);
      return T5_STREAM_AGAIN;
    }
    snapshot = *pairUsb;
    if (pendingUsb.tx != handle) {
      pendingUsb.tx = handle;
      pendingUsb.txSize = pendingUsb.txOffset = 0;
    }
    if (pendingUsb.txOffset == pendingUsb.txSize) {
      uint32_t fetched = 0;
      const auto result = registry.consume(owner, handle, pendingUsb.txBytes,
                                            sizeof(pendingUsb.txBytes), &fetched);
      if (result != T5_STREAM_OK && result != T5_STREAM_AGAIN) {
        usbTxRetry.store(false, std::memory_order_release);
        return result;
      }
      if (fetched > sizeof(pendingUsb.txBytes)) {
        usbTxRetry.store(false, std::memory_order_release);
        return T5_STREAM_IO;
      }
      pendingUsb.txOffset = 0;
      pendingUsb.txSize = fetched;
      if (!fetched) {
        usbTxRetry.store(false, std::memory_order_release);
        return T5_STREAM_AGAIN;
      }
    }
    requested = pendingUsb.txSize - pendingUsb.txOffset;
    std::memcpy(outgoing, pendingUsb.txBytes + pendingUsb.txOffset, requested);
    pendingUsb.txInFlight = true;
    usbTxRetry.store(true, std::memory_order_release);
  }
  uint32_t written = 0;
  const auto result = usbWrite(&snapshot, outgoing, requested, &written);
  Lock lock;
  pendingUsb.txInFlight = false;
  if (!pairUsb || pairTx != handle || pairUsb->epoch != snapshot.epoch ||
      pendingUsb.tx != handle) {
    usbTxRetry.store(false, std::memory_order_release);
    return T5_STREAM_DISCONNECTED;
  }
  if (written > requested || (result != T5_STREAM_OK && result != T5_STREAM_AGAIN)) {
    // Keep the pending bytes for diagnosis/recovery, but do not spin on a
    // persistent physical error. Never silently treat a short write as done.
    usbTxRetry.store(false, std::memory_order_release);
    return written > requested ? T5_STREAM_IO : result;
  }
  pendingUsb.txOffset += written;
  if (pendingUsb.txOffset == pendingUsb.txSize)
    pendingUsb.txOffset = pendingUsb.txSize = 0;
  // One additional bounded scheduler pass also drains bytes queued while the
  // previous class write was in flight; an empty endpoint clears this flag.
  usbTxRetry.store(true, std::memory_order_release);
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
    // class write. The pending TX buffer is never consumed twice.
    (void)drainPairTx();
  }
  Lock lock;
  const auto guard = guardGnssStream(h);
  if (guard != T5_STREAM_OK) return guard;
  const auto result = registry.read(owner, h, d, n, out);
  wake(); return result;
}
int32_t writeStream(t5_stream_t h, const void* d, uint32_t n, uint32_t* out) {
  if (out) *out = 0;
  if (!authorized()) return T5_STREAM_DENIED;
  if (h && (h == pairRx || h == pairTx)) {
    const auto transport = pairTransport(h);
    if (transport < 0) return transport;
  }
  int32_t result;
  {
    Lock lock;
    const auto guard = guardGnssStream(h);
    if (guard != T5_STREAM_OK) return guard;
    result = registry.write(owner, h, d, n, out);
  }
  if (result == T5_STREAM_OK && h && h == pairTx) (void)drainPairTx();
  wake(); return result;
}
int32_t finish(t5_stream_t h) { SESSION_CALL(registry.finish(owner, h)); }
int32_t seek(t5_stream_t h, uint64_t offset) { SESSION_CALL(registry.seek(owner, h, offset)); }
int32_t closeStream(t5_stream_t h) {
  if (!authorized()) return T5_STREAM_DENIED;
  int32_t result;
  {
    Lock lock;
    result = registry.close(owner, h);
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
  if (mutex) {
    Lock lock;
    gnss.releaseOwner(id);
    gnssGrants.releaseOwner(id);
    registry.release(id);
    notePairClosed(pairRx);
    notePairClosed(pairTx);
  }
  wake();
}
void releaseSerial(void*, uint32_t) { nativeSerialPortsEnd(); }
} // namespace
