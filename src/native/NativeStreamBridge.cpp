#include "NativeStreamBridge.h"
#include "NativeSerialPortBridge.h"
#include "NativeUsbDeviceRegistry.h"
#include <Arduino.h>
#include "runtime/streams/StreamRuntime.h"
#include "runtime/resources/ExecutionContext.h"
#include "runtime/capabilities/GnssStreamAuthority.h"
#include "network/HttpDownloader.h"
#include <T5AppApi.h>
#include <T5UsbApi.h>
#include <HalStorage.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <algorithm>
#include <cstring>
#include <new>
#include <string>

namespace {
RuntimeStreams::Registry registry;
// Exactly the existing stream registry is injected. Do not create a second
// position registry, provider queue, or cross-task ELF driver callback.
RuntimeStreams::LiveGnssSession gnss(RuntimeDevices::systemRegistry(), registry);
RuntimeDevices::GnssStreamAuthority gnssGrants(RuntimeDevices::systemCapabilityAccess(),
                                               RuntimeDevices::systemRegistry());
RuntimeResources::ExecutionContext invocation;
SemaphoreHandle_t mutex = nullptr;
TaskHandle_t scheduler = nullptr;
uint32_t owner = 0;
bool active = false, httpBusy = false, usbOpen = false;
uint32_t usbRefs = 0;
struct Lock {
  Lock() { xSemaphoreTake(mutex, portMAX_DELAY); }
  ~Lock() { xSemaphoreGive(mutex); }
};
bool authorized() {
  return active && invocation.running(owner) && t5_app_get_api(T5_APP_ABI_VERSION);
}
void wake() { if (scheduler) xTaskNotifyGive(scheduler); }
void schedule(void*) {
  TickType_t wait = portMAX_DELAY;
  for (;;) {
    ulTaskNotifyTake(pdTRUE, wait);
    Lock lock;
    // GNSS sources are forbidden in generic pipes until authorization is
    // propagated to downstream buffers. No scheduler thread reads consent.
    registry.pump();
    wait = registry.runnable() ? pdMS_TO_TICKS(10) : portMAX_DELAY;
  }
}
bool initialize() {
  // Called only from the native app's main task.
  if (!mutex) mutex = xSemaphoreCreateMutex();
  if (!mutex) return false;
  if (!scheduler && xTaskCreate(schedule, "stream-pipes", 4096, nullptr, 1, &scheduler) != pdPASS) return false;
  return true;
}

// Caller already holds the existing stream mutex. Check the *issued consent*
// immediately before any operation on a previously returned GNSS handle, not
// just when location->subscribe() or location->poll() is called. Revocation
// destroys the stream and its queued records rather than letting stale data
// drain into an app or a newly consented subscription.
int32_t guardGnssStream(t5_stream_t handle) {
  const auto result = gnssGrants.check(owner, handle);
  if (result != RuntimeDevices::GnssStreamAuthority::Check::Denied) return T5_STREAM_OK;
  RuntimeDevices::GnssStreamAuthority::Entry entry{};
  if (gnssGrants.findStream(owner, handle, &entry)) {
    (void)gnss.unsubscribe(owner, entry.subscription);
    (void)gnssGrants.forgetStream(owner, handle);
  }
  return T5_STREAM_DENIED;
}
void forgetClosedGnssStream(t5_stream_t handle) {
  RuntimeDevices::GnssStreamAuthority::Entry entry{};
  if (!gnssGrants.findStream(owner, handle, &entry)) return;
  (void)gnss.unsubscribe(owner, entry.subscription);
  (void)gnssGrants.forgetStream(owner, handle);
}

struct File {
  HalFile file;
  uint64_t length = 0, position = 0;
};
int32_t fileRead(void* ctx, void* data, uint32_t size, uint32_t* count) {
  auto& f = *static_cast<File*>(ctx);
  if (!Storage.ready() || !f.file.isOpen()) return T5_STREAM_IO;
  if (f.position == f.length) return T5_STREAM_EOF;
  auto n = f.file.read(data, static_cast<size_t>(std::min<uint64_t>(size, f.length - f.position)));
  if (n <= 0) return T5_STREAM_IO;
  *count = n; f.position += n; return T5_STREAM_OK;
}
int32_t fileWrite(void* ctx, const void* data, uint32_t size, uint32_t* count) {
  auto& f = *static_cast<File*>(ctx);
  if (!Storage.ready() || !f.file.isOpen()) return T5_STREAM_IO;
  *count = f.file.write(data, size);
  return *count == size ? T5_STREAM_OK : T5_STREAM_IO;
}
int32_t fileSeek(void* ctx, uint64_t offset) {
  auto& f = *static_cast<File*>(ctx);
  if (offset > f.length) return T5_STREAM_INVALID;
  if (!f.file.seek64(offset)) return T5_STREAM_IO;
  f.position = offset; return T5_STREAM_OK;
}
int32_t fileFinish(void* ctx) {
  auto& f = *static_cast<File*>(ctx);
  return f.file.close() ? T5_STREAM_OK : T5_STREAM_IO;
}
void fileClose(void* ctx) { delete static_cast<File*>(ctx); }
bool filePath(const char* path) {
  if (!path || strnlen(path, 256) >= 256 || std::strncmp(path, "/sd/", 4) || !path[4]) return false;
  const char* segment = path + 4;
  while (*segment) {
    const char* end = segment;
    while (*end && *end != '/') { if (*end == '\\') return false; ++end; }
    auto len = end - segment;
    if (!len || (len == 1 && segment[0] == '.') || (len == 2 && segment[0] == '.' && segment[1] == '.')) return false;
    if (!*end) return true;
    segment = end + 1;
  }
  return false;
}
int32_t openBuffer(uint32_t capacity, t5_stream_t* out) {
  if (out) *out = 0;
  if (!authorized()) return T5_STREAM_DENIED;
  Lock lock; return registry.buffer(owner, capacity, out);
}
int32_t openFile(const char* path, uint32_t mode, t5_stream_t* out) {
  if (out) *out = 0;
  if (!authorized()) return T5_STREAM_DENIED;
  if (!out || !filePath(path)) return T5_STREAM_INVALID;
  if (mode != T5_STREAM_FILE_READ && mode != T5_STREAM_FILE_CREATE_NEW) return T5_STREAM_UNSUPPORTED;
  Lock lock;
  if (!Storage.ready()) return T5_STREAM_IO;
  auto* f = new (std::nothrow) File;
  if (!f) return T5_STREAM_LIMIT;
  f->file = Storage.open(path + 3, mode == T5_STREAM_FILE_READ ? O_RDONLY : (O_WRONLY | O_CREAT | O_EXCL));
  if (!f->file || f->file.isDirectory()) { delete f; return T5_STREAM_IO; }
  f->length = f->file.fileSize64();
  RuntimeStreams::Provider p{f, fileRead, fileWrite, fileSeek, fileFinish, fileClose};
  auto flags = mode == T5_STREAM_FILE_READ ? T5_STREAM_READ | T5_STREAM_SEEK : T5_STREAM_WRITE;
  auto r = registry.attach(owner, T5_STREAM_BYTES, flags, p, out);
  if (r != T5_STREAM_OK) {
    delete f;
    if (mode == T5_STREAM_FILE_CREATE_NEW) Storage.remove(path + 3);
  }
  return r;
}
struct Usb {
  const t5_usb_api_v1* api;
  uint32_t epoch;
  bool direct = false;
  bool physicalReady = false;
};
Usb* directUsb = nullptr;
t5_stream_t directHandle = 0;
bool usbRevoked(const Usb& u) {
  return u.epoch == UINT32_MAX || nativeUsbProviderEpoch() != u.epoch;
}
int32_t usbState(Usb& u) {
  if (usbRevoked(u)) return T5_STREAM_DISCONNECTED;
  if (u.direct && !u.physicalReady) return T5_STREAM_AGAIN;
  t5_usb_serial_state_t s{};
  if (!u.api->serial_read_state(&s)) return T5_STREAM_IO;
  if (usbRevoked(u)) return T5_STREAM_DISCONNECTED;
  if (s.status == T5_USB_STATUS_ERROR) return T5_STREAM_IO;
  if (s.status == T5_USB_STATUS_OFF) return T5_STREAM_DISCONNECTED;
  if (!s.connected) return T5_STREAM_AGAIN;
  return s.status == T5_USB_STATUS_READY ? T5_STREAM_OK : T5_STREAM_AGAIN;
}
int32_t usbRead(void* ctx, void* data, uint32_t size, uint32_t* count) {
  auto& u = *static_cast<Usb*>(ctx);
  auto r = usbState(u); if (r != T5_STREAM_OK) return r;
  *count = u.api->serial_read(static_cast<uint8_t*>(data), size);
  if (usbRevoked(u)) { *count = 0; return T5_STREAM_DISCONNECTED; }
  return *count ? T5_STREAM_OK : T5_STREAM_AGAIN;
}
int32_t usbWrite(void* ctx, const void* data, uint32_t size, uint32_t* count) {
  auto& u = *static_cast<Usb*>(ctx);
  auto r = usbState(u); if (r != T5_STREAM_OK) return r;
  *count = u.api->serial_write(static_cast<const uint8_t*>(data), size);
  if (usbRevoked(u)) { *count = 0; return T5_STREAM_DISCONNECTED; }
  return *count ? T5_STREAM_OK : T5_STREAM_AGAIN;
}
void usbClose(void* ctx) {
  auto* u = static_cast<Usb*>(ctx);
  if (directUsb == u) { directUsb = nullptr; directHandle = 0; }
  delete u;
  if (usbRefs) --usbRefs;
  if (!usbRefs) usbOpen = false;
}
int32_t refreshDirect(t5_stream_t handle) {
  uint32_t expectedEpoch = 0;
  {
    Lock lock;
    if (!directUsb || handle != directHandle) return T5_STREAM_OK;
    expectedEpoch = directUsb->epoch;
  }
  const bool claimed = nativeUsbDirectStreamClaim(expectedEpoch);
  if (nativeUsbProviderEpoch() != expectedEpoch) return T5_STREAM_DISCONNECTED;
  if (!claimed) return T5_STREAM_BUSY;
  const bool ready = nativeUsbDirectStreamBound();
  {
    Lock lock;
    if (directUsb && directHandle == handle && directUsb->epoch == expectedEpoch)
      directUsb->physicalReady = ready;
  }
  return T5_STREAM_OK;
}
int32_t openUsb(t5_stream_t* out) {
  if (out) *out = 0;
  if (!authorized()) return T5_STREAM_DENIED;
  if (!out) return T5_STREAM_INVALID;
  const auto* api = t5_usb_get_api(T5_USB_API_VERSION);
  if (!api || !api->supported || !api->supported()) return T5_STREAM_UNSUPPORTED;
  if (!initialize()) return T5_STREAM_LIMIT;
  {
    Lock lock;
    if (usbOpen) return T5_STREAM_BUSY;
  }
  const uint32_t expectedEpoch = nativeUsbProviderEpoch();
  if (!nativeUsbDirectStreamClaim(expectedEpoch))
    return nativeUsbProviderEpoch() != expectedEpoch ? T5_STREAM_DISCONNECTED : T5_STREAM_BUSY;
  const bool ready = nativeUsbDirectStreamBound();
  int32_t result = T5_STREAM_BUSY;
  {
    Lock lock;
    if (!usbOpen) {
      auto* u = new (std::nothrow) Usb{api, expectedEpoch, true, ready};
      if (!u) result = T5_STREAM_LIMIT;
      else {
        RuntimeStreams::Provider p{u, usbRead, usbWrite, nullptr, nullptr, usbClose};
        result = registry.attach(owner, T5_STREAM_BYTES,
                                 T5_STREAM_READ | T5_STREAM_WRITE, p, out);
        if (result != T5_STREAM_OK) delete u;
        else {
          usbOpen = true;
          usbRefs = 1;
          directUsb = u;
          directHandle = *out;
        }
      }
    }
  }
  if (result != T5_STREAM_OK && result != T5_STREAM_BUSY) nativeUsbDirectStreamRelease();
  return result;
}
struct HttpJob { uint32_t owner; t5_stream_t stream; char url[1024]; };
class HttpSink final : public Stream {
 public:
  explicit HttpSink(HttpJob& job) : job_(job) {}
  size_t write(uint8_t b) override { return write(&b, 1); }
  size_t write(const uint8_t* data, size_t size) override {
    size_t done = 0;
    auto progress = millis();
    while (done < size) {
      uint32_t n = 0;
      int32_t r;
      { Lock lock; r = registry.produce(job_.owner, job_.stream, data + done,
          static_cast<uint32_t>(std::min<size_t>(size - done, T5_STREAM_CHUNK)), &n); }
      done += n;
      if (r < 0 || r == T5_STREAM_CLOSED) return done;
      if (n) { progress = millis(); wake(); }
      else if (millis() - progress >= 30000) {
        Lock lock; registry.finish(job_.owner, job_.stream, T5_STREAM_TIMEOUT); return done;
      } else delay(5);
    }
    return done;
  }
  int available() override { return 0; }
  int read() override { return -1; }
  int peek() override { return -1; }
  void flush() override {}
 private:
  HttpJob& job_;
};
void httpRequest(void* context) {
  auto* job = static_cast<HttpJob*>(context);
  HttpSink sink(*job);
  bool ok = HttpDownloader::fetchUrl(job->url, sink);
  { Lock lock; registry.finish(job->owner, job->stream, ok ? T5_STREAM_EOF : T5_STREAM_IO); httpBusy = false; }
  delete job;
  wake();
  vTaskDelete(nullptr);
}
int32_t openHttp(const char* url, t5_stream_t* out) {
  if (out) *out = 0;
  if (!authorized()) return T5_STREAM_DENIED;
  if (!out || !url || strnlen(url, 1024) >= 1024 ||
      (std::strncmp(url, "https://", 8) && std::strncmp(url, "http://", 7))) return T5_STREAM_INVALID;
  Lock lock;
  if (httpBusy) return T5_STREAM_BUSY;
  auto* job = new (std::nothrow) HttpJob{};
  if (!job) return T5_STREAM_LIMIT;
  auto r = registry.buffer(owner, T5_STREAM_MAX_BUFFER, out, T5_STREAM_READ);
  if (r != T5_STREAM_OK) { delete job; return r; }
  job->owner = owner; job->stream = *out; std::strcpy(job->url, url);
  httpBusy = true;
  if (xTaskCreate(httpRequest, "stream-http", 8192, job, 1, nullptr) != pdPASS) {
    httpBusy = false; registry.close(owner, *out); *out = 0; delete job; return T5_STREAM_LIMIT;
  }
  return r;
}
#define SESSION_CALL(expr) do { if (!authorized()) return T5_STREAM_DENIED; Lock lock; auto r = (expr); wake(); return r; } while (0)
int32_t readStream(t5_stream_t h, void* d, uint32_t n, uint32_t* out) {
  if (out) *out = 0;
  if (!authorized()) return T5_STREAM_DENIED;
  const auto claim = refreshDirect(h);
  if (claim != T5_STREAM_OK) return claim;
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
  Lock lock;
  const auto guard = guardGnssStream(h);
  if (guard != T5_STREAM_OK) return guard;
  const auto result = registry.write(owner, h, d, n, out);
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
    // An app may call the generic close instead of location->unsubscribe.
    // Release the GNSS subscriber's device grant and clear its consent token.
    forgetClosedGnssStream(h);
    (void)gnss.beforePoll(gnss.owner()); // Reconcile orphaned GNSS consumers on owner task.
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
  // Reject even a currently authorized GNSS pipe. Otherwise data copied to
  // an unprotected destination could survive subsequent consent revocation.
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
  if (mutex) { Lock lock; gnss.releaseOwner(id); gnssGrants.releaseOwner(id); registry.release(id); }
  nativeUsbDirectStreamRelease();
  wake();
}
void releaseSerial(void*, uint32_t) { nativeSerialPortsEnd(); }
}  // namespace

bool nativeStreamUsbIsBusy() {
  if (!authorized() || !initialize()) return true;
  Lock lock;
  return usbOpen;
}

t5_stream_result_t nativeStreamOpenUsbPair(t5_stream_t* rx, t5_stream_t* tx) {
  if (rx) *rx = 0;
  if (tx) *tx = 0;
  if (!authorized()) return T5_STREAM_DENIED;
  if (!rx || !tx) return T5_STREAM_INVALID;
  if (!initialize()) return T5_STREAM_LIMIT;
  const auto* usbApi = t5_usb_get_api(T5_USB_API_VERSION);
  if (!usbApi || !usbApi->supported || !usbApi->supported()) return T5_STREAM_UNSUPPORTED;

  Lock lock;
  if (usbOpen) return T5_STREAM_BUSY;
  const uint32_t sessionEpoch = nativeUsbProviderEpoch();
  auto* reader = new (std::nothrow) Usb{usbApi, sessionEpoch};
  auto* writer = new (std::nothrow) Usb{usbApi, sessionEpoch};
  if (!reader || !writer) {
    delete reader;
    delete writer;
    return T5_STREAM_LIMIT;
  }
  RuntimeStreams::Provider rp{reader, usbRead, nullptr, nullptr, nullptr, usbClose};
  auto result = registry.attach(owner, T5_STREAM_BYTES, T5_STREAM_READ, rp, rx);
  if (result != T5_STREAM_OK) {
    delete reader;
    delete writer;
    return result;
  }
  usbOpen = true;
  usbRefs = 1;
  RuntimeStreams::Provider wp{writer, nullptr, usbWrite, nullptr, nullptr, usbClose};
  result = registry.attach(owner, T5_STREAM_BYTES, T5_STREAM_WRITE, wp, tx);
  if (result != T5_STREAM_OK) {
    (void)registry.close(owner, *rx);
    *rx = 0;
    delete writer;
    return result;
  }
  usbRefs = 2;
  return T5_STREAM_OK;
}

t5_stream_result_t nativeStreamCloseOwned(t5_stream_t stream) {
  if (!active || invocation.id() != owner || !t5_app_get_api(T5_APP_ABI_VERSION))
    return T5_STREAM_DENIED;
  if (!stream || !initialize()) return T5_STREAM_INVALID;
  t5_stream_result_t result;
  bool closedDirect;
  {
    Lock lock;
    const bool wasDirect = directUsb && stream == directHandle;
    result = registry.close(owner, stream);
    closedDirect = wasDirect && !directUsb;
    forgetClosedGnssStream(stream);
  }
  if (closedDirect) nativeUsbDirectStreamRelease();
  wake();
  return result;
}

void nativeStreamsBegin() {
  if (!invocation.begin()) return;
  owner = invocation.id();
  active = true;
  if (!invocation.track(RuntimeResources::ExecutionContext::Resource::Streams, releaseStreams)) {
    invocation.end();
    active = false;
    return;
  }
  nativeSerialPortsBegin();
  if (!invocation.track(RuntimeResources::ExecutionContext::Resource::SerialPort, releaseSerial)) {
    nativeSerialPortsEnd();
    invocation.end();
    active = false;
  }
}
void nativeStreamsEnd() {
  invocation.end();
  active = false;
}
extern "C" const t5_stream_api_v1* t5_stream_get_api(uint32_t version) {
  if (!authorized() || !initialize()) return nullptr;
  if (version == T5_STREAM_API_VERSION) return &api;
  if (version == RISCRTE_STREAM_API_VERSION_2) return &api2.v1;
  return nullptr;
}

// All GNSS data-plane operations are marshalled through the SAME mutex and
// Registry above; the GPS ELF is polled by its claiming app task elsewhere.
t5_stream_result_t nativeGnssAttach(uint32_t providerOwner, uint32_t device, uint32_t borrowedSourceLease) {
  if (!authorized() || !initialize() || providerOwner != owner) return T5_STREAM_DENIED;
  Lock lock;
  return gnss.attach(providerOwner, device, borrowedSourceLease);
}
RuntimeStreams::LiveGnssSession::PollDecision nativeGnssBeforePoll(uint32_t providerOwner) {
  if (!authorized() || !initialize() || providerOwner != owner)
    return RuntimeStreams::LiveGnssSession::PollDecision::Denied;
  Lock lock;
  const auto result = gnss.beforePoll(providerOwner);
  wake();
  return result;
}
t5_stream_result_t nativeGnssPublishCopy(uint32_t providerOwner, const t5_gps_state_t& observation,
                                        uint32_t sampleMs) {
  if (!authorized() || !initialize() || providerOwner != owner) return T5_STREAM_DENIED;
  Lock lock;
  const auto result = gnss.publishCopied(providerOwner, observation, sampleMs);
  wake();
  return result;
}
void nativeGnssDisconnect(uint32_t providerOwner) {
  // Firmware teardown may be called after context enters Stopping.
  if (!mutex || !providerOwner || invocation.id() != providerOwner) return;
  Lock lock;
  if (gnss.owner() == providerOwner) gnss.disconnect();
  wake();
}
t5_stream_result_t nativeGnssSubscribe(uint32_t authenticatedOwner, uint32_t authorizedDevice,
                                      uint32_t issuedReadConsent,
                                      uint64_t* subscription, t5_stream_t* stream) {
  if (subscription) *subscription = 0;
  if (stream) *stream = 0;
  if (!authorized() || !initialize() || authenticatedOwner != owner ||
      !authorizedDevice || !issuedReadConsent || !subscription || !stream) return T5_STREAM_DENIED;
  Lock lock;
  if (gnss.device() != authorizedDevice) return T5_STREAM_DISCONNECTED;
  const auto result = gnss.subscribe(authenticatedOwner, subscription, stream);
  if (result != T5_STREAM_OK) return result;
  // A grant may disappear during synchronous stream allocation. Never expose
  // an unguarded queue, including during a partially completed subscription.
  if (!gnssGrants.bind(authenticatedOwner, authorizedDevice, issuedReadConsent,
                       *subscription, *stream)) {
    (void)gnss.unsubscribe(authenticatedOwner, *subscription);
    *subscription = 0;
    *stream = 0;
    return T5_STREAM_DENIED;
  }
  return T5_STREAM_OK;
}
t5_stream_result_t nativeGnssUnsubscribe(uint32_t authenticatedOwner, uint64_t subscription) {
  if (!active || invocation.id() != authenticatedOwner || !mutex) return T5_STREAM_DENIED;
  Lock lock;
  const auto result = gnss.unsubscribe(authenticatedOwner, subscription);
  if (result == T5_STREAM_OK || result == T5_STREAM_INVALID)
    (void)gnssGrants.forgetSubscription(authenticatedOwner, subscription);
  return result;
}
