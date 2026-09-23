// Actual dlopen provider -> module/graph -> production stream registry.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wreturn-type"
#define main unused_existing_bridge_fixture
#include "bridge_test.cpp"
#undef main
#pragma GCC diagnostic pop
#include "runtime/drivers/ProviderGraphV2.h"
#include "../drivers/provider_stream_fixture.h"
#include <freertos/task.h>
namespace {
struct EndSchedulerTurn {};
unsigned schedulerWaits = 0;
void waitOnce() { if (++schedulerWaits == 2) throw EndSchedulerTurn{}; }
void pumpTurn() {
  schedulerWaits = 0; testTaskWaitHook = waitOnce;
  try { testStreamTask(nullptr); } catch (const EndSchedulerTurn&) {}
  testTaskWaitHook = nullptr;
  assert(schedulerWaits == 2);
}
}
int main(int argc, char** argv) {
  assert(argc == 3);
  using namespace RuntimeProviders;
  const SpecV2 spec{"fixture-streams", argv[1], "fixture.streams", 1, nullptr, 0};
  {
    GraphV2 missingHost;
    assert(missingHost.addVerified(spec));
    assert(!missingHost.acquire("fixture.streams", 1).slot);
    assert(missingHost.shutdown());
  }
  GraphV2 graph(nativeProviderStreamHost());
  assert(graph.addVerified(spec));
  auto grant = graph.acquire("fixture.streams", 1); assert(grant.slot);
  const auto* provider = static_cast<const provider_stream_fixture_api*>(graph.interfaceFor(grant));
  assert(provider);
  assert(provider->polls() == 0);
  const uint32_t beforePoll = fakeTime;
  graph.poll([]() { return fakeTime; }, []() { ++fakeTime; });
  assert(provider->polls() == 1 && fakeTime == beforePoll + 1);
  const auto host = *provider->streams();
  const auto source = provider->source();
  uint32_t n = 99; char bytes[16]{};
  assert(host.produce(host.context, source, "abcdefghij", 10, &n) == 0 && n == 8);
  assert(host.produce(host.context, source, "x", 1, &n) == T5_STREAM_AGAIN && n == 0);
  assert(host.consume(host.context, source, bytes, 8, &n) == T5_STREAM_DENIED && n == 0);
  // A foreground app cannot claim another context's endpoint by handle alone.
  nativeStreamsBegin(); api = t5_stream_get_api(1); assert(api);
  assert(api->read(source, bytes, 8, &n) == T5_STREAM_INVALID && n == 0);
  t5_stream_t appBuffer = 0; assert(api->open_buffer(8, &appBuffer) == 0);
  assert(host.produce(host.context, appBuffer, "x", 1, &n) == T5_STREAM_INVALID && n == 0);
  const uint32_t consumer = nativeProviderStreamConsumer(); assert(consumer);
  assert(graph.grantStream(grant, consumer, source, T5_STREAM_READ));
  assert(!graph.grantStream(grant, consumer, appBuffer, T5_STREAM_READ));
  t5_pipe_t pipe = 0;
  assert(api->pipe_connect(source, appBuffer, 0, &pipe) == 0);
  auto shared = graph.acquire("fixture.streams", 1); assert(shared.slot);
  assert(graph.grantStream(shared, consumer, source, T5_STREAM_READ));
  assert(graph.release(grant)); // A second lease retains the same read right.
  assert(!graph.grantStream(grant, consumer, source, T5_STREAM_READ));
  grant = shared;
  pumpTurn();
  assert(api->read(appBuffer, bytes, 8, &n) == 0 && n == 8 && !memcmp(bytes, "abcdefgh", 8));
  assert(api->pipe_close(pipe) == 0);
  assert(host.produce(host.context, source, "x", 1, &n) == 0 && n == 1);
  auto keeper = graph.acquire("fixture.streams", 1); assert(keeper.slot);
  assert(graph.release(grant)); // Provider stays active; sole stream grant ends.
  assert(api->read(source, bytes, 8, &n) == T5_STREAM_INVALID && n == 0);
  grant = keeper;
  assert(graph.grantStream(grant, consumer, source, T5_STREAM_READ));
  nativeStreamsEnd(); // Provider remains independently mapped and live.
  nativeStreamsBegin(); api = t5_stream_get_api(1); assert(api);
  assert(api->read(source, bytes, 8, &n) == T5_STREAM_INVALID && n == 0);
  nativeStreamsEnd();
  assert(host.finish(host.context, source, T5_STREAM_EOF) == 0);
  assert(host.produce(host.context, source, "x", 1, &n) == T5_STREAM_CLOSED && n == 0);
  risc_stream_endpoint_v1 desc{sizeof(desc), T5_STREAM_BYTES, 3, 8, nullptr, 0, 0};
  uint32_t loop = 0;
  assert(host.publish(host.context, &desc, &loop) == 0);
  assert(host.produce(host.context, loop, "abc", 3, &n) == 0 && n == 3);
  assert(host.consume(host.context, loop, bytes, 2, &n) == 0 && n == 2 && !memcmp(bytes, "ab", 2));
  assert(host.consume(host.context, loop, bytes, 8, &n) == 0 && n == 1 && bytes[0] == 'c');
  desc.kind = T5_STREAM_RECORDS; desc.schema = "fixture.record.v1";
  desc.max_record = 8; desc.record_capacity = 2;
  uint32_t records = 0, extra = 0, denied = 99;
  assert(host.publish(host.context, &desc, &records) == 0);
  assert(host.produce_record(host.context, records, "abcd", 4) == 0);
  assert(host.consume_record(host.context, records, bytes, 2, &n) == T5_STREAM_LIMIT && n == 0);
  assert(host.consume_record(host.context, records, bytes, 8, &n) == 0 && n == 4 && !memcmp(bytes, "abcd", 4));
  assert(host.publish(host.context, &desc, &extra) == 0);
  assert(host.publish(host.context, &desc, &denied) == T5_STREAM_LIMIT && denied == 0);
  assert(host.close(host.context, loop) == 0);
  assert(host.publish(host.context, &desc, &denied) == 0 && denied != loop);
  assert(host.consume(host.context, loop, bytes, 8, &n) == T5_STREAM_INVALID && n == 0);
  nativeStreamsBegin(); api = t5_stream_get_api(1);
  const auto* recordsApi = riscrte_stream_get_api_v2(); assert(recordsApi);
  t5_stream_t recordSink = 0;
  assert(recordsApi->open_record_buffer(desc.schema, 8, 2, 3, &recordSink) == 0);
  assert(graph.grantStream(grant, nativeProviderStreamConsumer(), records, T5_STREAM_READ));
  assert(api->pipe_connect(records, recordSink, 0, &pipe) == 0);
  assert(host.produce_record(host.context, records, "record", 6) == 0);
  assert(host.consume_record(host.context, records, bytes, 8, &n) == T5_STREAM_BUSY && n == 0);
  pumpTurn();
  assert(recordsApi->record_read(recordSink, bytes, 8, &n) == 0 && n == 6 && !memcmp(bytes, "record", 6));
  // Quarantine revokes queues immediately but retains the ELF and host table.
  provider->block_quiesce(true);
  assert(!graph.release(grant) && !graph.interfaceFor(grant));
  graph.poll([]() { return fakeTime; }, []() { ++fakeTime; });
  assert(provider->polls() == 1); // Revoked/quarantined ELF is never polled.
  t5_pipe_info_t pipeState{}; pipeState.struct_size = sizeof(pipeState);
  assert(api->pipe_info(pipe, &pipeState) == 0 && pipeState.state == T5_PIPE_FAILED &&
         pipeState.last_error == T5_STREAM_DISCONNECTED);
  nativeStreamsEnd();
  assert(host.produce_record(host.context, records, "x", 1) == T5_STREAM_DENIED);
  assert(host.publish(host.context, &desc, &denied) == T5_STREAM_DENIED && denied == 0);
  provider->block_quiesce(false);
  assert(graph.release(grant));
  auto again = graph.acquire("fixture.streams", 1); assert(again.slot);
  const auto* replacement = static_cast<const provider_stream_fixture_api*>(graph.interfaceFor(again));
  assert(replacement->streams()->context != host.context);
  assert(host.produce(host.context, source, "x", 1, &n) == T5_STREAM_DENIED && n == 0);
  assert(graph.release(again) && graph.shutdown());
  GraphV2 failed(nativeProviderStreamHost());
  const SpecV2 bad{"fixture-streams", argv[2], "fixture.streams", 1, nullptr, 0};
  assert(failed.addVerified(bad));
  for (unsigned i = 0; i < 20; ++i) assert(!failed.acquire("fixture.streams", 1).slot);
  assert(failed.shutdown()); // No leaked queue/context slots after failed start.
  puts("Loaded provider stream contexts, isolation, records and quiescence passed");
}
