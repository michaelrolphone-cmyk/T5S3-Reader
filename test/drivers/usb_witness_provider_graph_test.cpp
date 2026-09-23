#include <Arduino.h>
#include <HalStorage.h>
#include <T5AppApi.h>
#include <T5SerialPortApi.h>
#include <T5StreamApi.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include "native/NativeStreamBridge.h"
#include "native/NativeSerialPortBridge.h"
#include "runtime/drivers/InstalledProviderGraph.h"
#include "network/HttpDownloader.h"
#include "witness_controller_fixture.h"
#include <dlfcn.h>
#include <cassert>
#include <cstdio>
#include <cstring>
uint32_t fakeTime = 0;
void (*httpTask)(void*) = nullptr;
void* httpContext = nullptr;
std::map<std::string, std::shared_ptr<TestFile>> files;
bool storageReady = true, closeOk = true;
HalStorage Storage;
t5_app_api_v1 app{};
extern "C" const t5_app_api_v1* t5_app_get_api(uint32_t) { return &app; }
bool HttpDownloader::fetchUrl(const std::string&, Stream&, const std::string&, const std::string&) { return false; }
RuntimeProviders::GraphV2* installedGraph = nullptr;
namespace RuntimeInstalledProviders {
bool prepare() { return installedGraph != nullptr; }
void poll() { installedGraph->poll([]() { return fakeTime; }, []() { ++fakeTime; }); }
bool nextProvider(const char* capability, uint32_t version, size_t* cursor, char* id, size_t capacity) {
  while (*cursor < installedGraph->moduleCount()) {
    const char* found = installedGraph->matchingProviderId((*cursor)++, capability, version);
    if (found) { std::snprintf(id, capacity, "%s", found); return true; }
  }
  return false;
}
bool acquire(const char* id, const char* capability, uint32_t version, Lease* out) {
  out->grant = installedGraph->acquireFrom(id, capability, version);
  out->interface = installedGraph->interfaceFor(out->grant);
  return out->interface != nullptr;
}
bool release(Lease* lease) {
  if (!installedGraph->release(lease->grant)) return false;
  *lease = {}; return true;
}
bool attachStream(const Lease& lease, uint32_t endpoint, uint32_t rights) {
  assert(testStreamMutexDepth == 0);
  return installedGraph->interfaceFor(lease.grant) == lease.interface &&
      installedGraph->grantStream(lease.grant, nativeProviderStreamConsumer(), endpoint, rights);
}
bool recoverFailedProvider(const char* id, const char* capability, uint32_t version) {
  return installedGraph->recoverFailedFrom(id, capability, version);
}
}
namespace {
struct StopTurn {};
unsigned waits = 0;
void stopTurn() { if (++waits == 2) throw StopTurn{}; }
void turn() {
  nativeDeviceDiscoveryTick();
  waits = 0; testTaskWaitHook = stopTurn;
  try { testStreamTask(nullptr); } catch (const StopTurn&) {}
  testTaskWaitHook = nullptr;
  assert(testStreamMutexDepth == 0);
}
}
int main(int argc, char** argv) {
  assert(argc == 4);
  void* monitor = dlopen(argv[1], RTLD_NOW); assert(monitor);
  auto statsFn = reinterpret_cast<witness_controller_stats*(*)()>(dlsym(monitor, "fixture_witness_stats"));
  assert(statsFn); auto* stats = statsFn();
  RuntimeProviders::GraphV2 graph(nativeProviderStreamHost()); installedGraph = &graph;
  const RuntimeProviders::RequirementV2 hostNeeds[] = {{"usb.controller", 1}};
  const RuntimeProviders::RequirementV2 serialNeeds[] = {{"usb.host", 1}};
  assert(graph.addVerified({"fixture-witness-controller", argv[1], "usb.controller", 1, nullptr, 0}));
  assert(graph.addVerified({"usb-host-v2", argv[2], "usb.host", 1, hostNeeds, 1}));
  assert(graph.addVerified({"usb-serial-witness", argv[3], "serial.port", 1, serialNeeds, 1}));
  nativeStreamsBegin();
  const auto* serial = t5_serial_port_get_api(1); const auto* api = t5_stream_get_api(1);
  assert(serial && api);
  nativeDeviceDiscoveryTick();
  assert(RuntimeDevices::systemRegistry().count() == 1);
  t5_serial_port_request_t request{};
  request.config = {115200, 8, T5_SERIAL_PARITY_NONE, 1, T5_SERIAL_FLOW_NONE};
  t5_serial_port_lease_t lease = 0; t5_stream_t rx = 0, tx = 0;
  t5_stream_t blockers[11]{};
  for (auto& h : blockers) assert(api->open_buffer(8, &h) == 0);
  assert(serial->acquire(&request, &lease, &rx, &tx) == T5_SERIAL_DENIED && !lease && !rx && !tx);
  assert(!nativeSerialProviderActive() && !nativeStreamSerialIsBusy());
  for (const auto h : blockers) assert(api->close(h) == 0);
  assert(serial->acquire(&request, &lease, &rx, &tx) == 0);
  assert(!nativeSerialProviderInventoryStopChecked());
  assert(!nativeSerialProviderActive() && !nativeStreamSerialIsBusy()); // No resident shuttle.
  assert(serial->set_control_lines(lease, 1, 0) == 0);
  t5_serial_port_state_t state{}; assert(serial->read_status(lease, &state) == 0 && state.dtr && !state.rts);
  t5_stream_t source = 0, sink = 0; t5_pipe_t input = 0, output = 0;
  assert(api->open_buffer(32, &source) == 0 && api->open_buffer(8, &sink) == 0);
  uint32_t n = 0; char bytes[16]{};
  assert(api->write(source, "abcdef", 6, &n) == 0 && n == 6);
  assert(api->pipe_connect(rx, sink, 0, &input) == 0);
  assert(api->pipe_connect(source, tx, 0, &output) == 0);
  stats->blocked = true;
  for (unsigned i = 0; i < 5; ++i) turn();
  assert(stats->writes && stats->used == 0);
  assert(api->read(sink, bytes, 8, &n) == 0 && n && !memcmp(bytes, "NEW", 3));
  stats->blocked = false;
  for (unsigned i = 0; i < 5; ++i) turn();
  assert(stats->used == 6 && !memcmp(stats->transmitted, "abcdef", 6));
  assert(stats->read_timeout == 1 && stats->write_timeout == 1);
  for (unsigned i = 0; i < 1000; ++i) turn();
  const unsigned fullReads = stats->reads;
  for (unsigned i = 0; i < 5; ++i) turn();
  assert(stats->reads == fullReads); // Saturated RX stops physical reads.
  stats->close_error = true;
  assert(serial->release(lease) == T5_SERIAL_IO);
  const unsigned stoppedReads = stats->reads;
  for (unsigned i = 0; i < 5; ++i) turn();
  assert(stats->reads == stoppedReads);
  t5_pipe_info_t info{}; info.struct_size = sizeof(info);
  assert(api->pipe_info(input, &info) == 0 && info.state == T5_PIPE_FAILED);
  stats->close_error = false;
  assert(serial->release(lease) == 0);
  assert(api->pipe_close(input) == 0 && api->pipe_close(output) == 0);
  const t5_stream_t oldRx = rx;
  assert(serial->acquire(&request, &lease, &rx, &tx) == 0 && rx != oldRx);
  assert(api->read(oldRx, bytes, sizeof(bytes), &n) < 0 && n == 0);
  assert(api->pipe_connect(source, tx, 0, &output) == 0);
  assert(api->write(source, "error", 5, &n) == 0 && n == 5);
  stats->write_error = true;
  for (unsigned i = 0; i < 3; ++i) turn();
  assert(api->pipe_info(output, &info) == 0 && info.state == T5_PIPE_FAILED && info.last_error == T5_STREAM_IO);
  const unsigned failedWrites = stats->writes;
  for (unsigned i = 0; i < 3; ++i) turn();
  assert(stats->writes == failedWrites);
  assert(serial->release(lease) == 0);
  stats->write_error = false;
  assert(api->pipe_close(output) == 0);
  assert(serial->acquire(&request, &lease, &rx, &tx) == 0);
  assert(api->pipe_connect(rx, sink, 0, &input) == 0);
  stats->detached = true; turn();
  assert(api->pipe_info(input, &info) == 0 && info.state == T5_PIPE_FAILED &&
         info.last_error == T5_STREAM_DISCONNECTED);
  assert(api->read(rx, bytes, sizeof(bytes), &n) < 0 && n == 0);
  assert(serial->release(lease) == 0);
  assert(api->pipe_close(input) == 0);
  stats->detached = false; turn();
  assert(serial->acquire(&request, &lease, &rx, &tx) == 0);
  assert(serial->release(lease) == 0);
  nativeStreamsEnd();
  assert(nativeSerialProviderInventoryStopChecked());
  assert(graph.shutdown()); installedGraph = nullptr;
  assert(dlclose(monitor) == 0);
  puts("Packaged witness -> installed serial -> shared pipes, backpressure and close retry: PASS");
}
