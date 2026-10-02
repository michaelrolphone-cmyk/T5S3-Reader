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
static bool classStarted = false;
bool nativeUsbClassAvailable() { return true; }
bool nativeUsbClassStart(const t5_serial_config_t& config) {
  if (classStarted) return false;
  ++starts; ++configs;
  usbStatus.line_coding.baud_rate = config.baud_rate;
  usbStatus.line_coding.data_bits = config.data_bits;
  usbStatus.line_coding.parity = config.parity;
  usbStatus.line_coding.stop_bits = config.stop_bits;
  classStarted = true;
  return true;
}
void nativeUsbClassStop() {
  ++stops;
  classStarted = false;
  nativeUsbProviderDetach();
  usbStatus.status = T5_USB_STATUS_OFF;
  usbStatus.connected = 0;
}
bool nativeUsbClassConfigure(const t5_serial_config_t& config) {
  ++lineChanges;
  usbStatus.line_coding.baud_rate = config.baud_rate;
  usbStatus.line_coding.data_bits = config.data_bits;
  usbStatus.line_coding.parity = config.parity;
  usbStatus.line_coding.stop_bits = config.stop_bits;
  return true;
}
bool nativeUsbClassControl(bool dtr, bool rts) {
  usbStatus.dtr = dtr;
  usbStatus.rts = rts;
  return true;
}
bool nativeUsbClassReadState(t5_usb_serial_state_t* out) {
  if (!out) return false;
  *out = usbStatus;
  return true;
}
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
  assert(!t5_stream_get_api(1) && !riscrte_stream_get_api_v2());
  nativeStreamsBegin(); api = t5_stream_get_api(1);
  const auto* recordApi = riscrte_stream_get_api_v2();
  assert(api && recordApi && !t5_stream_get_api(3));
  assert(api->api_version == T5_STREAM_API_VERSION && api->struct_size == sizeof(t5_stream_api_v1));
  assert(recordApi->v1.api_version == RISCRTE_STREAM_API_VERSION_2 &&
         recordApi->v1.struct_size == sizeof(riscrte_stream_api_v2));
  assert(recordApi->v1.open_buffer == api->open_buffer && recordApi->v1.pipe_connect == api->pipe_connect);
  assert(recordApi->open_record_buffer && recordApi->record_read && recordApi->record_write && recordApi->record_info);
  t5_stream_t h, other;
  uint32_t count;
  uint8_t bytes[512];

  t5_stream_t recordSource = 0, recordSink = 0, recordWrong = 0, recordSmall = 0;
  assert(recordApi->open_record_buffer("location.fix", 16, 2, 3, &h) == T5_STREAM_INVALID && !h);
  assert(recordApi->open_record_buffer("location.fix.v1", 16, 2, 4, &h) == T5_STREAM_INVALID && !h);
  assert(recordApi->open_record_buffer("location.fix.v1", 16, 2, 3, &recordSource) == T5_STREAM_OK);
  assert(recordApi->open_record_buffer("location.fix.v1", 16, 1, 3, &recordSink) == T5_STREAM_OK);
  assert(recordApi->open_record_buffer("location.fix.v2", 16, 1, 3, &recordWrong) == T5_STREAM_OK);
  assert(recordApi->open_record_buffer("location.fix.v1", 8, 1, 3, &recordSmall) == T5_STREAM_OK);
  assert(api->read(recordSource, bytes, 4, &count) == T5_STREAM_UNSUPPORTED && count == 0);
  assert(api->write(recordSource, "abc", 3, &count) == T5_STREAM_UNSUPPORTED && count == 0);
  riscrte_record_info_v1 recordMetadata{};
  recordMetadata.struct_size = sizeof(recordMetadata) - 1;
  assert(recordApi->record_info(recordSource, &recordMetadata) == T5_STREAM_INVALID);
  recordMetadata.struct_size = sizeof(recordMetadata);
  assert(recordApi->record_info(recordSource, &recordMetadata) == T5_STREAM_OK);
  assert(std::strcmp(recordMetadata.schema, "location.fix.v1") == 0 && recordMetadata.max_record == 16 &&
         recordMetadata.capacity_records == 2 && recordMetadata.queued_records == 0 && recordMetadata.owner);
  assert(recordApi->record_write(recordSource, "abc", 3) == T5_STREAM_OK);
  assert(recordApi->record_write(recordSource, nullptr, 0) == T5_STREAM_OK);
  assert(recordApi->record_write(recordSource, "x", 1) == T5_STREAM_AGAIN);
  assert(recordApi->record_read(recordSource, bytes, 2, &count) == T5_STREAM_LIMIT && count == 0);
  assert(recordApi->record_read(recordSource, bytes, 3, &count) == T5_STREAM_OK && count == 3 &&
         std::memcmp(bytes, "abc", 3) == 0);
  assert(recordApi->record_read(recordSource, nullptr, 0, &count) == T5_STREAM_OK && count == 0);
  assert(recordApi->record_read(recordSource, bytes, 3, &count) == T5_STREAM_AGAIN && count == 0);
  assert(recordApi->record_info(recordSource, &recordMetadata) == T5_STREAM_OK &&
         recordMetadata.records_written == 2 && recordMetadata.records_read == 2);
  t5_pipe_t recordPipe = 0;
  assert(api->pipe_connect(recordSource, recordWrong, 0, &recordPipe) == T5_STREAM_UNSUPPORTED && !recordPipe);
  assert(api->pipe_connect(recordSource, recordSmall, 0, &recordPipe) == T5_STREAM_LIMIT && !recordPipe);
  assert(api->open_buffer(8, &h) == T5_STREAM_OK);
  assert(api->pipe_connect(recordSource, h, 0, &recordPipe) == T5_STREAM_UNSUPPORTED && !recordPipe);
  assert(recordApi->record_info(h, &recordMetadata) == T5_STREAM_UNSUPPORTED);
  assert(api->close(h) == T5_STREAM_OK);
  assert(api->pipe_connect(recordSource, recordSink, 0, &recordPipe) == T5_STREAM_OK && recordPipe);
  assert(recordApi->record_read(recordSource, bytes, 3, &count) == T5_STREAM_BUSY);
  assert(recordApi->record_write(recordSink, "x", 1) == T5_STREAM_BUSY);
  assert(api->pipe_cancel(recordPipe) == T5_STREAM_OK && api->pipe_close(recordPipe) == T5_STREAM_OK);
  assert(api->close(recordSink) == T5_STREAM_OK);
  assert(api->close(recordWrong) == T5_STREAM_OK);
  assert(api->close(recordSmall) == T5_STREAM_OK);
  const t5_stream_t staleRecord = recordSource;
  assert(api->close(recordSource) == T5_STREAM_OK);
  assert(recordApi->record_read(staleRecord, bytes, 3, &count) == T5_STREAM_INVALID);

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

  usbStatus.status = T5_USB_STATUS_READY; usbStatus.connected = 1;
  std::strcpy(usbStatus.product, "Legacy UART"); usbStatus.vid = 0x1234; usbStatus.pid = 0x5678;
  nativeUsbProviderAttach(&usbStatus, 0);
  assert(api->open_usb(&h) == T5_STREAM_UNSUPPORTED && !h);
  assert(api->open_usb(&other) == T5_STREAM_UNSUPPORTED && !other);
  nativeUsbProviderDetach();

  const auto* serial = t5_serial_port_get_api(T5_SERIAL_PORT_API_VERSION);
  assert(serial && serial->capability_id && std::string(serial->capability_id) == "serial.port");
  t5_serial_port_request_t request{};
  request.config = {115200u, 8u, T5_SERIAL_PARITY_NONE, 1u, T5_SERIAL_FLOW_NONE};
  t5_serial_port_lease_t lease = 0, busyLease = 0;
  t5_stream_t rx = 0, tx = 0, busyRx = 0, busyTx = 0;
  usbStatus.status = T5_USB_STATUS_READY; usbStatus.connected = 1;
  std::strcpy(usbStatus.product, "Test UART"); usbStatus.vid = 0x1234; usbStatus.pid = 0x5678;
  assert(serial->acquire(&request, &lease, &rx, &tx) == T5_SERIAL_OK);
  nativeUsbProviderAttach(&usbStatus, 2);
  assert(lease && rx && tx && rx != tx && starts == 1);
  assert(serial->acquire(&request, &busyLease, &busyRx, &busyTx) == T5_SERIAL_BUSY);
  assert(api->write(rx, "a", 1, &count) == T5_STREAM_DENIED);
  assert(api->read(tx, bytes, 1, &count) == T5_STREAM_DENIED);
  assert(api->read(rx, bytes, 3, &count) == T5_STREAM_AGAIN && count == 0);
  assert(api->write(tx, "abc", 3, &count) == T5_STREAM_OK && count == 3);
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

  usbStatus.status = T5_USB_STATUS_READY; usbStatus.connected = 1;
  t5_serial_port_lease_t second = 0;
  assert(serial->acquire(&request, &second, &rx, &tx) == T5_SERIAL_OK && second != stale && starts == 2);
  nativeUsbProviderAttach(&usbStatus, 2);
  assert(serial->read_status(second, &serialState) == T5_SERIAL_OK && serialState.device != firstDevice);
  assert(api->write(tx, "abc", 3, &count) == T5_STREAM_OK && count == 3);
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

  assert(stops == 2);
  assert(t5_serial_port_get_api(T5_SERIAL_PORT_API_VERSION));
  nativeStreamsEnd();
  assert(api->close(replacement) == T5_STREAM_DENIED && !t5_stream_get_api(1));
  assert(!riscrte_stream_get_api_v2());
  assert(!t5_serial_port_get_api(T5_SERIAL_PORT_API_VERSION));
  std::cout << "Stream firmware adapter tests passed\n";
}
