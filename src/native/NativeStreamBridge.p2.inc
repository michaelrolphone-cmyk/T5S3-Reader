int32_t pairTransport(t5_stream_t handle) {
  Usb* session;
  {
    Lock lock;
    if (handle != pairRx && handle != pairTx) return T5_STREAM_OK;
    session = pairUsb;
  }
  if (!session) return T5_STREAM_DISCONNECTED;
  return usbState(*session);
}
int32_t fillPairRx() {
  Usb* session;
  t5_stream_t handle;
  {
    Lock lock;
    session = pairUsb;
    handle = pairRx;
  }
  if (!session || !handle) return T5_STREAM_OK;
  uint8_t incoming[T5_STREAM_CHUNK];
  uint32_t count = 0;
  const auto result = usbRead(session, incoming, sizeof(incoming), &count);
  if (result < 0 || !count) return result;
  uint32_t wrote = 0;
  Lock lock;
  if (pairUsb != session || pairRx != handle) return T5_STREAM_DISCONNECTED;
  return registry.produce(owner, handle, incoming, count, &wrote);
}
int32_t drainPairTx() {
  Usb* session;
  t5_stream_t handle;
  uint8_t outgoing[T5_STREAM_CHUNK];
  uint32_t count = 0;
  int32_t result;
  {
    Lock lock;
    session = pairUsb;
    handle = pairTx;
    if (!session || !handle) return T5_STREAM_OK;
    result = registry.consume(owner, handle, outgoing, sizeof(outgoing), &count);
  }
  if (result < 0 || !count) return result;
  uint32_t wrote = 0;
  return usbWrite(session, outgoing, count, &wrote);
}
#define SESSION_CALL(expr) do { if (!authorized()) return T5_STREAM_DENIED; Lock lock; auto r = (expr); wake(); return r; } while (0)
int32_t readStream(t5_stream_t h, void* d, uint32_t n, uint32_t* out) {
  if (out) *out = 0;
  if (!authorized()) return T5_STREAM_DENIED;
  const auto claim = refreshDirect(h);
  if (claim != T5_STREAM_OK) return claim;
  if (h && (h == pairRx || h == pairTx)) {
    const auto transport = pairTransport(h);
    if (transport < 0) return transport;
  }
  if (h && h == pairRx) {
    const auto filled = fillPairRx();
    if (filled < 0) return filled;
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
  const auto claim = refreshDirect(h);
  if (claim != T5_STREAM_OK) return claim;
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
  bool closedDirect;
  {
    Lock lock;
    const bool wasDirect = directUsb && h == directHandle;
    result = registry.close(owner, h);
    closedDirect = wasDirect && !directUsb;
    if (result == T5_STREAM_OK) notePairClosed(h);
    forgetClosedGnssStream(h);
    (void)gnss.beforePoll(gnss.owner());
  }
  if (closedDirect) nativeUsbDirectStreamRelease();
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
  const auto src = refreshDirect(s);
  if (src != T5_STREAM_OK) return src;
  const auto dst = refreshDirect(d);
  if (dst != T5_STREAM_OK) return dst;
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
  nativeUsbDirectStreamRelease();
  wake();
}
void releaseSerial(void*, uint32_t) { nativeSerialPortsEnd(); }
}  // namespace
