#include "NativeStreamBridge.h"
#include "NativeSerialPortBridge.h"
#include <Arduino.h>
#include "runtime/streams/StreamRuntime.h"
#include "runtime/resources/ExecutionContext.h"
#include "runtime/capabilities/GnssStreamAuthority.h"
#include "network/HttpDownloader.h"
#include <T5AppApi.h>
#include <HalStorage.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <algorithm>
#include <atomic>
#include <cstring>
#include <new>
#include <string>

namespace {
RuntimeStreams::Registry registry;
RuntimeStreams::LiveGnssSession gnss(RuntimeDevices::systemRegistry(), registry);
RuntimeDevices::GnssStreamAuthority gnssGrants(RuntimeDevices::systemCapabilityAccess(),
                                               RuntimeDevices::systemRegistry());
RuntimeResources::ExecutionContext invocation;
SemaphoreHandle_t mutex = nullptr;
TaskHandle_t scheduler = nullptr;
uint32_t owner = 0;
bool active = false, httpBusy = false, serialOpen = false;
uint32_t serialRefs = 0;
// The already-running stream task also retries a bounded pending serial write.
// No hardware transfer occurs while its registry mutex is held.
std::atomic<bool> serialTxRetry{false};
int32_t drainPairTx();
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
    if (serialTxRetry.load(std::memory_order_acquire)) (void)drainPairTx();
    // At most four external chunks or ten milliseconds per turn, then really
    // block even when notifications or a permanently AGAIN provider persist.
    const uint32_t started = millis();
    unsigned operations = 0;
    for (;;) {
      RuntimeStreams::ExternalIo io{};
      {
        Lock lock;
        if (!registry.pumpPrepare(&io)) {
          wait = (registry.runnable() || serialTxRetry.load(std::memory_order_acquire))
                     ? pdMS_TO_TICKS(10) : portMAX_DELAY;
          break;
        }
      }
      uint8_t incoming[T5_STREAM_CHUNK];
      uint32_t count = 0;
      int32_t result = T5_STREAM_UNSUPPORTED;
      if (io.reading) {
        result = io.provider.read ? io.provider.read(io.provider.context, incoming, io.request, &count)
                                  : T5_STREAM_UNSUPPORTED;
      } else {
        result = io.provider.write
                     ? io.provider.write(io.provider.context, io.payload.data(), io.request, &count)
                     : T5_STREAM_UNSUPPORTED;
      }
      {
        Lock lock;
        registry.pumpComplete(io, result, io.reading ? incoming : nullptr, count);
      }
      if (++operations >= RuntimeStreams::Registry::MaxPipes ||
          static_cast<uint32_t>(millis() - started) >= 10) {
        wait = pdMS_TO_TICKS(10);
        vTaskDelay(1);
        break;
      }
    }
  }
}
bool initialize() {
  if (!mutex) mutex = xSemaphoreCreateMutex();
  if (!mutex) return false;
  if (!scheduler && xTaskCreate(schedule, "stream-pipes", 4096, nullptr, 1, &scheduler) != pdPASS) return false;
  return true;
}

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
struct SerialLink {
  uint32_t epoch;
};
SerialLink* pairSerial = nullptr;
t5_stream_t pairRx = 0, pairTx = 0;
void notePairClosed(t5_stream_t handle) {
  if (handle && handle == pairRx) pairRx = 0;
  if (handle && handle == pairTx) pairTx = 0;
  if (pairRx || pairTx) return;
  serialTxRetry.store(false, std::memory_order_release);
  delete pairSerial;
  pairSerial = nullptr;
  serialOpen = false;
  serialRefs = 0;
}
bool serialRevoked(const SerialLink& link) {
  const uint32_t current = nativeSerialProviderEpoch();
  return !link.epoch || !current || current != link.epoch;
}
int32_t serialState(const SerialLink& link) {
  if (serialRevoked(link) || !nativeSerialProviderActive())
    return T5_STREAM_DISCONNECTED;
  return T5_STREAM_OK;
}
int32_t serialReadProvider(void* ctx, void* data, uint32_t size, uint32_t* count) {
  auto& link = *static_cast<SerialLink*>(ctx);
  const auto state = serialState(link);
  if (state != T5_STREAM_OK) return state;
  const auto result = nativeSerialProviderRead(static_cast<uint8_t*>(data), size, count);
  if (serialRevoked(link)) { if (count) *count = 0; return T5_STREAM_DISCONNECTED; }
  return result;
}
int32_t serialWriteProvider(void* ctx, const void* data, uint32_t size, uint32_t* count) {
  auto& link = *static_cast<SerialLink*>(ctx);
  const auto state = serialState(link);
  if (state != T5_STREAM_OK) return state;
  const auto result = nativeSerialProviderWrite(
      static_cast<const uint8_t*>(data), size, count);
  if (serialRevoked(link)) { if (count) *count = 0; return T5_STREAM_DISCONNECTED; }
  return result;
}
int32_t openUsb(t5_stream_t* out) {
  if (out) *out = 0;
  if (!authorized()) return T5_STREAM_DENIED;
  return T5_STREAM_UNSUPPORTED;
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
