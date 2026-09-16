#include "NativeStreamBridge.h"
#include <Arduino.h>
#include "runtime/streams/StreamRuntime.h"
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
SemaphoreHandle_t mutex = nullptr;
TaskHandle_t scheduler = nullptr;
uint32_t owner = 0;
bool active = false, httpBusy = false, usbOpen = false;
struct Lock {
  Lock() { xSemaphoreTake(mutex, portMAX_DELAY); }
  ~Lock() { xSemaphoreGive(mutex); }
};
bool authorized() { return active && t5_app_get_api(T5_APP_ABI_VERSION); }
void wake() { if (scheduler) xTaskNotifyGive(scheduler); }
void schedule(void*) {
  TickType_t wait = portMAX_DELAY;
  for (;;) {
    ulTaskNotifyTake(pdTRUE, wait);
    Lock lock;
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
    if (!len || (len == 1 && *segment == '.') || (len == 2 && segment[0] == '.' && segment[1] == '.')) return false;
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
struct Usb { const t5_usb_api_v1* api; };
int32_t usbState(Usb& u) {
  t5_usb_serial_state_t s{};
  if (!u.api->serial_read_state(&s)) return T5_STREAM_IO;
  if (s.status == T5_USB_STATUS_ERROR) return T5_STREAM_IO;
  // A physical unplug is transient while the USB host session is still running.
  // Returning DISCONNECTED here would make StreamRuntime permanently terminalize
  // this handle, preventing it from receiving from a replacement/replugged device.
  if (s.status == T5_USB_STATUS_OFF) return T5_STREAM_DISCONNECTED;
  if (!s.connected) return T5_STREAM_AGAIN;
  return s.status == T5_USB_STATUS_READY ? T5_STREAM_OK : T5_STREAM_AGAIN;
}
int32_t usbRead(void* ctx, void* data, uint32_t size, uint32_t* count) {
  auto& u = *static_cast<Usb*>(ctx);
  auto r = usbState(u); if (r < 0) return r;
  *count = u.api->serial_read(static_cast<uint8_t*>(data), size);
  return *count ? T5_STREAM_OK : T5_STREAM_AGAIN;
}
int32_t usbWrite(void* ctx, const void* data, uint32_t size, uint32_t* count) {
  auto& u = *static_cast<Usb*>(ctx);
  auto r = usbState(u); if (r != T5_STREAM_OK) return r;
  *count = u.api->serial_write(static_cast<const uint8_t*>(data), size);
  return *count ? T5_STREAM_OK : T5_STREAM_AGAIN;
}
void usbClose(void* ctx) { delete static_cast<Usb*>(ctx); usbOpen = false; }
int32_t openUsb(t5_stream_t* out) {
  if (out) *out = 0;
  if (!authorized()) return T5_STREAM_DENIED;
  if (!out) return T5_STREAM_INVALID;
  const auto* api = t5_usb_get_api(T5_USB_API_VERSION);
  if (!api || !api->supported()) return T5_STREAM_UNSUPPORTED;
  Lock lock;
  if (usbOpen) return T5_STREAM_BUSY;
  auto* u = new (std::nothrow) Usb{api};
  if (!u) return T5_STREAM_LIMIT;
  RuntimeStreams::Provider p{u, usbRead, usbWrite, nullptr, nullptr, usbClose};
  auto r = registry.attach(owner, T5_STREAM_BYTES, T5_STREAM_READ | T5_STREAM_WRITE, p, out);
  if (r != T5_STREAM_OK) delete u; else usbOpen = true;
  return r;
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
  // HTTPClient handles Content-Length, redirects and chunked transfer decoding.
  // Only firmware-owned data survives caller unload; stale handles reject writes.
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
  return T5_STREAM_OK;
}
#define SESSION_CALL(expr) do { if (!authorized()) return T5_STREAM_DENIED; Lock lock; auto r = (expr); wake(); return r; } while (0)
int32_t readStream(t5_stream_t h, void* d, uint32_t n, uint32_t* out) {
  if (out) *out = 0;
  SESSION_CALL(registry.read(owner, h, d, n, out));
}
int32_t writeStream(t5_stream_t h, const void* d, uint32_t n, uint32_t* out) {
  if (out) *out = 0;
  SESSION_CALL(registry.write(owner, h, d, n, out));
}
int32_t finish(t5_stream_t h) { SESSION_CALL(registry.finish(owner, h)); }
int32_t seek(t5_stream_t h, uint64_t offset) { SESSION_CALL(registry.seek(owner, h, offset)); }
int32_t closeStream(t5_stream_t h) { SESSION_CALL(registry.close(owner, h)); }
int32_t info(t5_stream_t h, t5_stream_info_t* out) { SESSION_CALL(registry.info(owner, h, out)); }
int32_t connect(t5_stream_t s, t5_stream_t d, uint32_t policy, t5_pipe_t* out) {
  if (out) *out = 0;
  SESSION_CALL(registry.connect(owner, s, d, policy, out));
}
int32_t pause(t5_pipe_t h, uint32_t paused) { SESSION_CALL(registry.pause(owner, h, paused != 0)); }
int32_t cancel(t5_pipe_t h) { SESSION_CALL(registry.cancel(owner, h)); }
int32_t closePipe(t5_pipe_t h) { SESSION_CALL(registry.closePipe(owner, h)); }
int32_t pipeInfo(t5_pipe_t h, t5_pipe_info_t* out) { SESSION_CALL(registry.pipeInfo(owner, h, out)); }
#undef SESSION_CALL
const t5_stream_api_v1 api = {T5_STREAM_API_VERSION, sizeof(t5_stream_api_v1), openBuffer, openFile, openUsb,
  openHttp, readStream, writeStream, finish, seek, closeStream, info, connect, pause, cancel, closePipe, pipeInfo};
}
void nativeStreamsBegin() {
  // Exhaustion fails closed rather than reusing execution-context identity.
  if (owner != UINT32_MAX) { ++owner; active = true; }
}
void nativeStreamsEnd() {
  active = false;
  if (mutex) { Lock lock; registry.release(owner); }
}
extern "C" const t5_stream_api_v1* t5_stream_get_api(uint32_t version) {
  if (version != T5_STREAM_API_VERSION || !authorized() || !initialize()) return nullptr;
  return &api;
}