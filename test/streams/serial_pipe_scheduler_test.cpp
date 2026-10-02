// Exercise the real scheduler and installed serial bridges. Only the provider
// inventory, physical I/O, storage, and RTOS are host fixtures.
#define RISCRTE_TEST_REAL_STREAM_BRIDGE
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wreturn-type"
#define main unused_installed_fixture_main
#include "installed_serial_bridge_test.cpp"
#undef main
#pragma GCC diagnostic pop
#include <Arduino.h>
#include <HalStorage.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include "native/NativeStreamBridge.h"
#include "network/HttpDownloader.h"
#include <algorithm>
uint32_t fakeTime = 0;
void (*httpTask)(void*) = nullptr;
void* httpContext = nullptr;
std::map<std::string, std::shared_ptr<TestFile>> files;
bool storageReady = true, closeOk = true;
HalStorage Storage;
bool HttpDownloader::fetchUrl(const std::string&, Stream&, const std::string&, const std::string&) { return false; }
namespace {
struct StopTurn {};
unsigned waits = 0, reads = 0, writes = 0;
size_t incoming = 0;
std::vector<uint8_t> sent;
bool blocked = true, failWrite = false;
void stopAfterTurn() { if (++waits == 2) throw StopTurn{}; }
void turn() {
  waits = 0; testTaskWaitHook = stopAfterTurn;
  try { testStreamTask(nullptr); } catch (const StopTurn&) {}
  testTaskWaitHook = nullptr;
  assert(waits == 2 && testStreamMutexDepth == 0);
}
int32_t receive(uint64_t token, uint8_t* data, size_t size, uint32_t ms) {
  assert(testStreamMutexDepth == 0 && token == fourthToken + 1000 && ms == 1);
  ++reads;
  const size_t n = std::min(size, incoming);
  std::fill(data, data + n, uint8_t('r')); incoming -= n;
  return static_cast<int32_t>(n);
}
int32_t transmit(uint64_t token, const uint8_t* data, size_t size, uint32_t ms) {
  assert(testStreamMutexDepth == 0 && token == fourthToken + 1000 && ms == 1);
  ++writes;
  if (failWrite) return -1;
  if (blocked) return 0;
  const size_t n = std::min<size_t>(3, size);
  sent.insert(sent.end(), data, data + n);
  return static_cast<int32_t>(n);
}
}
int main() {
  nativeStreamsBegin();
  const auto* api = t5_stream_get_api(1); assert(api && testStreamTask);
  const auto* serial = t5_serial_port_get_api(1); assert(serial);
  nativeDeviceDiscoveryTick();
  providerReadHook = receive; providerWriteHook = transmit;
  t5_serial_port_request_t request{}; request.config = coding;
  t5_serial_port_lease_t lease = 0;
  t5_stream_t rx = 0, tx = 0, source = 0, sink = 0;
  assert(serial->acquire(&request, &lease, &rx, &tx) == T5_SERIAL_OK);
  assert(lastOpenedDevice == 44 && !classBound);
  assert(api->open_buffer(32, &source) == 0);
  assert(api->open_buffer(8, &sink) == 0);
  uint32_t n = 0;
  assert(api->write(source, "abcdefghijkl", 12, &n) == 0 && n == 12);
  t5_pipe_t input = 0, output = 0;
  assert(api->pipe_connect(rx, sink, 0, &input) == 0);
  assert(api->pipe_connect(source, tx, 0, &output) == 0);
  incoming = 10000;
  turn();
  char bytes[8];
  assert(api->read(sink, bytes, sizeof(bytes), &n) == 0 && n == 8);
  assert(std::all_of(bytes, bytes + n, [](char c) { return c == 'r'; }));
  assert(reads && writes && sent.empty()); // No direct serial read or write.
  for (unsigned i = 0; i < 20; ++i) turn();
  const unsigned saturatedReads = reads;
  for (unsigned i = 0; i < 5; ++i) turn();
  assert(reads == saturatedReads); // Full queues stop physical reads.
  assert(api->read(sink, bytes, sizeof(bytes), &n) == 0 && n == 8);
  for (unsigned i = 0; i < 70; ++i) {
    turn();
    assert(api->read(sink, bytes, sizeof(bytes), &n) == 0 && n == 8);
  }
  assert(reads > saturatedReads);
  assert(api->pipe_pause(input, 1) == 0);
  const unsigned pausedReads = reads;
  for (unsigned i = 0; i < 3; ++i) turn();
  assert(reads == pausedReads);
  assert(api->pipe_pause(input, 0) == 0);
  // Cancel the TX pipe to release its write lease, leaving the accepted bytes
  // staged for physical output. finish must wait for those bytes too.
  assert(api->pipe_cancel(output) == 0);
  assert(api->finish(tx) == T5_STREAM_AGAIN);
  blocked = false;
  for (unsigned i = 0; i < 8; ++i) turn();
  assert(std::string(sent.begin(), sent.end()) == "abcdefghijkl");
  assert(api->finish(tx) == 0);
  const uint32_t oldEpoch = nativeSerialProviderEpoch();
  fourthToken = 0; nativeDeviceDiscoveryTick(); turn();
  t5_pipe_info_t info{}; info.struct_size = sizeof(info);
  assert(api->pipe_info(input, &info) == 0);
  assert(info.state == T5_PIPE_FAILED && info.last_error == T5_STREAM_DISCONNECTED);
  assert(serial->release(lease) == T5_SERIAL_OK);
  assert(api->pipe_close(input) == 0 && api->pipe_close(output) == 0);
  fourthToken = 45; nativeDeviceDiscoveryTick();
  t5_stream_t oldTx = tx;
  assert(serial->acquire(&request, &lease, &rx, &tx) == 0 && tx != oldTx);
  assert(api->write(oldTx, "x", 1, &n) < 0 && n == 0);
  const unsigned priorReads = reads, priorWrites = writes;
  uint8_t scratch = 0;
  assert(nativeSerialProviderRead(oldEpoch, &scratch, 1, &n) == T5_STREAM_DISCONNECTED && n == 0);
  assert(nativeSerialProviderWrite(oldEpoch, &scratch, 1, &n) == T5_STREAM_DISCONNECTED && n == 0);
  assert(reads == priorReads && writes == priorWrites);
  assert(api->write(source, "failure", 7, &n) == 0 && n == 7);
  assert(api->pipe_connect(source, tx, 0, &output) == 0);
  failWrite = true; turn();
  assert(api->pipe_info(output, &info) == 0);
  assert(info.state == T5_PIPE_FAILED && info.last_error == T5_STREAM_IO);
  const unsigned failedWrites = writes;
  for (unsigned i = 0; i < 3; ++i) turn();
  assert(writes == failedWrites);
  assert(api->write(tx, "x", 1, &n) == T5_STREAM_IO && n == 0);
  assert(serial->release(lease) == 0);
  nativeStreamsEnd();
  puts("installed serial pipe scheduler tests passed");
}
