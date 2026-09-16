#include <Arduino.h>
#include <HalStorage.h>
#include <T5AppApi.h>
#include <T5SerialPortApi.h>
#include <T5UsbApi.h>
#include <T5StreamApi.h>
#include "native/NativeStreamBridge.h"
#include "native/NativeUsbDeviceRegistry.h"
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
static int starts = 0, stops = 0, configs = 0, lineChanges = 0;
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
  usb.serial_start = [](const t5_usb_line_coding_t* coding) {
    ++starts; ++configs; usbStatus.line_coding = *coding; return true;
  };
  usb.serial_stop = [] { ++stops; nativeUsbProviderDetach(); usbStatus.status = T5_USB_STATUS_OFF; usbStatus.connected = 0; };
  usb.serial_read_state = [](t5_usb_serial_state_t* out) { *out = usbStatus; return true; };
  usb.serial_set_line_coding = [](const t5_usb_line_coding_t* coding) {
    ++lineChanges; usbStatus.line_coding = *coding; return true;
  };
  usb.serial_set_control_lines = [](bool dtr, bool rts) {
    usbStatus.dtr = dtr; usbStatus.rts = rts; return true;
  };
  usb.serial_read = [](uint8_t*, size_t) -> size_t { return 0; };
  usb.serial_write = [](const uint8_t*, size_t n) -> size_t { return std::min<size_t>(2, n); };
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

  // Even the compatibility USB stream must never resume against a replacement
  // device after a real host DEV_GONE callback, including an identical replug.
  usbStatus.status = T5_USB_STATUS_READY; usbStatus.connected = 1;
  std::strcpy(usbStatus.product, "Legacy UART"); usbStatus.vid = 0x1234; usbStatus.pid = 0x5678;
  nativeUsbProviderAttach(&usbStatus, 0);
  assert(api->open_usb(&h) == 0);
  assert(api->open_usb(&other) == T5_STREAM_BUSY);
  assert(api->write(h, "abc", 3, &count) == 0 && count == 2);
  usbStatus.status = T5_USB_STATUS_CONFIGURING;
  assert(api->write(h, "abc", 3, &count) == T5_STREAM_AGAIN && count == 0);
  usbStatus.status = T5_USB_STATUS_READY;
  nativeUsbProviderDetach();
  assert(api->read(h, bytes, 3, &count) == T5_STREAM_DISCONNECTED && count == 0);
  nativeUsbProviderAttach(&usbStatus, 0);
  assert(api->write(h, "abc", 3, &count) == T5_STREAM_DISCONNECTED && count == 0);
  assert(api->close(h) == 0 && stops == 0);
  nativeUsbProviderDetach();
  assert(api->open_usb(&h) == 0); // Fresh handle may use a replacement.
  nativeUsbProviderAttach(&usbStatus, 0);
  assert(api->write(h, "abc", 3, &count) == T5_STREAM_OK && count == 2);
  assert(api->close(h) == 0 && stops == 0);
  nativeUsbProviderDetach();

  // serial.port is an exclusive semantic lease backed by independent RX/TX streams.
  const auto* serial = t5_serial_port_get_api(T5_SERIAL_PORT_API_VERSION);
  assert(serial && serial->capability_id && std::string(serial->capability_id) == "serial.port");
  t5_serial_port_request_t request{};
  request.config = {115200u, 8u, T5_SERIAL_PARITY_NONE, 1u, T5_SERIAL_FLOW_NONE};
  t5_serial_port_lease_t lease = 0, busyLease = 0;
  t5_stream_t rx = 0, tx = 0, busyRx = 0, busyTx = 0;
  usbStatus.status = T5_USB_STATUS_READY; usbStatus.connected = 1;
  std::strcpy(usbStatus.product, "Test UART"); usbStatus.vid = 0x1234; usbStatus.pid = 0x5678;
  assert(serial->acquire(&request, &lease, &rx, &tx) == T5_SERIAL_OK);
  nativeUsbProviderAttach(&usbStatus, 2); // Host config finished; real data interface 2 is claimed.
  assert(lease && rx && tx && rx != tx && starts == 1);
  assert(serial->acquire(&request, &busyLease, &busyRx, &busyTx) == T5_SERIAL_BUSY);
  assert(api->write(rx, "a", 1, &count) == T5_STREAM_DENIED);
  assert(api->read(tx, bytes, 1, &count) == T5_STREAM_DENIED);
  assert(api->read(rx, bytes, 3, &count) == T5_STREAM_AGAIN && count == 0);
  assert(api->write(tx, "abc", 3, &count) == T5_STREAM_OK && count == 2);
  t5_serial_port_state_t serialState{};
  assert(serial->read_status(lease, &serialState) == T5_SERIAL_OK);
  assert(serialState.status == T5_SERIAL_STATUS_READY && serialState.connected && serialState.device != 0);
  assert(std::string(serialState.device_label).find("Test UART") != std::string::npos);
  const auto firstDevice = serialState.device;

  auto changed = request.config; changed.baud_rate = 9600;
  assert(serial->configure(lease, &changed) == T5_SERIAL_OK && lineChanges == 1);
  changed.flow_control = T5_SERIAL_FLOW_RTS_CTS;
  assert(serial->configure(lease, &changed) == T5_SERIAL_UNSUPPORTED);
  assert(serial->set_control_lines(lease, false, true) == T5_SERIAL_OK && !usbStatus.dtr && usbStatus.rts);

  // DEV_GONE invalidates before USB's old status snapshot changes. Neither a
  // same-device replacement nor a missed status poll may revive this lease.
  nativeUsbProviderDetach();
  assert(serial->read_status(lease, &serialState) == T5_SERIAL_OK);
  assert(!serialState.connected && !serialState.device && serialState.status == T5_SERIAL_STATUS_WAITING);
  assert(serialState.last_error == T5_SERIAL_DISCONNECTED);
  nativeUsbProviderAttach(&usbStatus, 2);
  assert(serial->read_status(lease, &serialState) == T5_SERIAL_OK);
  assert(!serialState.connected && !serialState.device && serialState.last_error == T5_SERIAL_DISCONNECTED);
  assert(api->read(rx, bytes, 3, &count) == T5_STREAM_DISCONNECTED && count == 0);
  assert(api->write(tx, "abc", 3, &count) == T5_STREAM_DISCONNECTED && count == 0);
  assert(serial->configure(lease, &request.config) == T5_SERIAL_DISCONNECTED);
  assert(serial->set_control_lines(lease, true, false) == T5_SERIAL_DISCONNECTED);

  const auto stale = lease;
  assert(serial->release(lease) == T5_SERIAL_OK && stops == 1);
  assert(serial->configure(stale, &request.config) == T5_SERIAL_CLOSED);
  request.device = firstDevice;
  assert(serial->acquire(&request, &busyLease, &busyRx, &busyTx) == T5_SERIAL_INVALID && starts == 1);
  request.device = 0;

  // New acquisition gets new generation-safe lease and usable independent streams.
  usbStatus.status = T5_USB_STATUS_READY; usbStatus.connected = 1;
  t5_serial_port_lease_t second = 0;
  assert(serial->acquire(&request, &second, &rx, &tx) == T5_SERIAL_OK && second != stale && starts == 2);
  nativeUsbProviderAttach(&usbStatus, 2);
  assert(serial->read_status(second, &serialState) == T5_SERIAL_OK && serialState.device != firstDevice);
  assert(api->write(tx, "abc", 3, &count) == T5_STREAM_OK && count == 2);
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

  // unloadInRequest already ended/restarted the original context, reclaiming its serial lease.
  assert(stops == 2);
  assert(t5_serial_port_get_api(T5_SERIAL_PORT_API_VERSION));
  nativeStreamsEnd();
  assert(api->close(replacement) == T5_STREAM_DENIED && !t5_stream_get_api(1));
  assert(!t5_serial_port_get_api(T5_SERIAL_PORT_API_VERSION));
  std::cout << "Stream firmware adapter tests passed\n";
}
