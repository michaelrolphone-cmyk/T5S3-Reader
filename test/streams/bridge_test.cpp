#include <Arduino.h>
#include <HalStorage.h>
#include <T5AppApi.h>
#include <T5UsbApi.h>
#include <T5StreamApi.h>
#include "native/NativeStreamBridge.h"
#include "network/HttpDownloader.h"
#include <cassert>
#include <iostream>
uint32_t fakeTime = 0;
void (*httpTask)(void*) = nullptr;
void* httpContext = nullptr;
std::map<std::string, std::shared_ptr<TestFile>> files;
bool storageReady = true, closeOk = true;
HalStorage Storage;
static t5_app_api_v1 app{};
extern "C" const t5_app_api_v1* t5_app_get_api(uint32_t) { return &app; }
static t5_usb_api_v1 usb{};
static t5_usb_serial_state_t usbStatus{};
static int stops = 0;
extern "C" const t5_usb_api_v1* t5_usb_get_api(uint32_t) { return &usb; }
static unsigned bodySize = 3;
static bool unloadInRequest = false;
static const t5_stream_api_v1* api;
static t5_stream_t replacement;
bool HttpDownloader::fetchUrl(const std::string&, Stream& out, const std::string&, const std::string&) {
  if (unloadInRequest) {
    nativeStreamsEnd(); nativeStreamsBegin();
    assert(api->open_buffer(8, &replacement) == 0);
  }
  std::vector<uint8_t> bytes(bodySize, 'x');
  return out.write(bytes.data(), bytes.size()) == bytes.size();
}
int main() {
  usb.supported = [] { return true; };
  usb.serial_read_state = [](t5_usb_serial_state_t* out) { *out = usbStatus; return true; };
  usb.serial_read = [](uint8_t*, size_t) -> size_t { return 0; };
  usb.serial_write = [](const uint8_t*, size_t n) -> size_t { return std::min<size_t>(2, n); };
  usb.serial_stop = [] { ++stops; };
  assert(!t5_stream_get_api(1));
  nativeStreamsBegin(); api = t5_stream_get_api(1); assert(api && !t5_stream_get_api(2));
  t5_stream_t h, other;
  uint32_t count;
  uint8_t bytes[512];
  for (auto path : {"/outside/a", "/sd/../a", "/sd/./a", "/sd/a//b", "/sd/a/", "/sd/a\\b"})
    assert(api->open_file(path, T5_STREAM_FILE_READ, &h) == T5_STREAM_INVALID);
  files["/input"] = std::make_shared<TestFile>(); files["/input"]->data.resize(10000, 42);
  assert(api->open_file("/sd/input", T5_STREAM_FILE_READ, &h) == 0);
  assert(api->seek(h, 9999) == 0);
  assert(api->read(h, bytes, sizeof(bytes), &count) == 0 && count == 1 && bytes[0] == 42);
  assert(api->read(h, bytes, sizeof(bytes), &count) == T5_STREAM_EOF);
  assert(api->seek(h, 10001) == T5_STREAM_INVALID);
  assert(api->seek(h, 0) == 0);
  storageReady = false; assert(api->read(h, bytes, 1, &count) == T5_STREAM_IO); storageReady = true;
  assert(api->close(h) == 0);
  assert(api->open_file("/sd/input", T5_STREAM_FILE_CREATE_NEW, &h) == T5_STREAM_IO);
  assert(files["/input"]->data.size() == 10000);
  assert(api->open_file("/sd/output", T5_STREAM_FILE_CREATE_NEW, &h) == 0);
  assert(api->write(h, "abc", 3, &count) == 0 && count == 3);
  closeOk = false; assert(api->finish(h) == T5_STREAM_IO); closeOk = true;
  assert(api->close(h) == 0);
  assert(api->open_usb(&h) == 0);
  assert(api->open_usb(&other) == T5_STREAM_BUSY);
  usbStatus.status = T5_USB_STATUS_READY; usbStatus.connected = 1;
  assert(api->write(h, "abc", 3, &count) == 0 && count == 2);
  usbStatus.status = T5_USB_STATUS_CONFIGURING;
  assert(api->write(h, "abc", 3, &count) == T5_STREAM_AGAIN && count == 0);
  usbStatus.connected = 0; usbStatus.status = T5_USB_STATUS_WAITING;
  assert(api->read(h, bytes, 3, &count) == T5_STREAM_AGAIN && count == 0);
  usbStatus.connected = 1; usbStatus.status = T5_USB_STATUS_READY;
  assert(api->write(h, "abc", 3, &count) == 0 && count == 2);
  assert(api->close(h) == 0 && stops == 0);
  assert(api->open_http("https://example.test/file", &h) == 0);
  assert(api->open_http("https://example.test/other", &other) == T5_STREAM_BUSY);
  httpTask(httpContext);
  assert(api->read(h, bytes, 512, &count) == 0 && count == 3);
  assert(api->read(h, bytes, 512, &count) == T5_STREAM_EOF);
  assert(api->close(h) == 0);
  bodySize = 8192;
  assert(api->open_http("https://example.test/large", &h) == 0); httpTask(httpContext);
  unsigned total = 0; int result;
  do { result = api->read(h, bytes, 512, &count); total += count; } while (result == 0);
  assert(total == 4096 && result == T5_STREAM_TIMEOUT);
  assert(api->close(h) == 0);
  unloadInRequest = true;
  assert(api->open_http("https://example.test/late", &h) == 0); httpTask(httpContext);
  assert(api->read(h, bytes, 512, &count) == T5_STREAM_INVALID);
  assert(api->read(replacement, bytes, 512, &count) == T5_STREAM_AGAIN && count == 0);
  nativeStreamsEnd();
  assert(api->close(replacement) == T5_STREAM_DENIED && !t5_stream_get_api(1));
  std::cout << "Stream firmware adapter tests passed\n";
}
