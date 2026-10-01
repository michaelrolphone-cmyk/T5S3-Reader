// Actual dlopen provider -> module/graph -> production stream registry.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wreturn-type"
#define main unused_existing_bridge_fixture
#include "bridge_test.cpp"
#undef main
#pragma GCC diagnostic pop
#include "runtime/drivers/ProviderGraphV2.h"
#include "runtime/packages/PackageUseGate.h"
#include "../drivers/provider_stream_fixture.h"
#include <freertos/task.h>
#include <freertos/semphr.h>
namespace {
RuntimeProviders::GraphV2* ownerGraph = nullptr;
unsigned ownerCalls = 0;
void ownerProgress() {
  assert(testStreamMutexDepth == 0); // No provider code under the stream lock.
  ++ownerCalls;
  nativeProviderOwnerTick(); // Reentrant progress is ignored, not recursive.
  ownerGraph->poll([]() { return fakeTime; }, []() { ++fakeTime; });
}
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
  // A real ELF produces bytes/records and drains a sink with no input/device
  // discovery calls. The owner tick and ordinary stream operations suffice.
  GraphV2 autonomous(nativeProviderStreamHost());
  assert(autonomous.addVerified(spec));
  auto live = autonomous.acquire("fixture.streams", 1); assert(live.slot);
  const auto* producer = static_cast<const provider_stream_fixture_api*>(autonomous.interfaceFor(live));
  producer->automatic(true);
  ownerGraph = &autonomous;
  nativeProviderSetOwnerPoll(ownerProgress);
  nativeProviderOwnerTick(); // Owner loop also works without a foreground app.
  assert(ownerCalls == 1 && producer->polls() == 1);
  nativeStreamsBegin(); api = t5_stream_get_api(1);
  recordsApi = riscrte_stream_get_api_v2();
  const auto owner = nativeProviderStreamConsumer();
  assert(autonomous.grantStream(live, owner, producer->source(), T5_STREAM_READ));
  assert(autonomous.grantStream(live, owner, producer->records(), T5_STREAM_READ));
  assert(autonomous.grantStream(live, owner, producer->sink(), T5_STREAM_WRITE));
  assert(autonomous.grantStream(live, owner, producer->record_sink(), T5_STREAM_WRITE));
  uint32_t before = producer->polls();
  assert(api->read(producer->source(), bytes, sizeof(bytes), &n) == 0 && n && bytes[0] == 'P');
  assert(producer->polls() == before + 1);
  assert(recordsApi->record_read(producer->records(), bytes, sizeof(bytes), &n) == 0 && n == 1 && bytes[0] == 'R');
  assert(api->write(producer->sink(), "abc", 3, &n) == 0 && n == 3);
  nativeProviderOwnerTick();
  assert(producer->consumed() == 3);
  assert(recordsApi->record_write(producer->record_sink(), "S", 1) == 0);
  nativeProviderOwnerTick();
  assert(producer->records_consumed() == 1);
  t5_stream_t destination = 0;
  assert(api->open_buffer(8, &destination) == 0);
  assert(api->pipe_connect(producer->source(), destination, 0, &pipe) == 0);
  before = producer->polls();
  pipeState.struct_size = sizeof(pipeState);
  assert(api->pipe_info(pipe, &pipeState) == 0 && producer->polls() == before + 1);
  pumpTurn();
  assert(api->read(destination, bytes, sizeof(bytes), &n) == 0 && n && bytes[0] == 'P');
  assert(api->pipe_close(pipe) == 0);
  before = producer->polls();
  nativeStreamsEnd();
  assert(api->read(producer->source(), bytes, sizeof(bytes), &n) == T5_STREAM_DENIED);
  assert(producer->polls() == before); // Denied caller cannot schedule work.
  producer->block_quiesce(true);
  assert(!autonomous.release(live));
  nativeProviderOwnerTick();
  assert(producer->polls() == before); // Quarantine remains unscheduled.
  producer->block_quiesce(false);
  nativeProviderSetOwnerPoll(nullptr); ownerGraph = nullptr;
  assert(autonomous.release(live) && autonomous.shutdown());
  // Carry a manager-admitted scope through the owned graph and real ELF ABI.
  // The original caller's identity is mutated after registration to verify copy.
  {
    const char* root = "/Providers/fixture-streams";
    const std::string sha(64, '0');
    const std::string json = std::string("{\"schema\":2,\"kind\":\"provider\",\"id\":\"fixture-streams\",\"version\":\"1.0.0\","
        "\"artifact\":\"driver.elf\",\"architecture\":\"xtensa-esp32s3\",\"min_runtime_api\":1,\"entries\":["
        "{\"name\":\"driver.elf\",\"size_bytes\":52,\"sha256\":\"") + sha + "\",\"executable\":true},"
        "{\"name\":\"assets/text.txt\",\"size_bytes\":3,\"sha256\":\"" + sha + "\",\"executable\":false}],\"requires\":[]}";
    auto metadata = std::make_shared<TestFile>(); metadata->data.assign(json.begin(), json.end());
    files[std::string(root) + "/.package.json"] = metadata;
    auto payload = std::make_shared<TestFile>(); payload->data = {'a', 'b', 'c'};
    files[std::string(root) + "/assets/text.txt"] = payload;
    auto& gate = RuntimePackages::systemPackageUseGate(); assert(gate.pin(root));
    SpecV2 scoped = spec;
    assert(RuntimePackages::makeIdentity(RuntimePackages::Kind::Provider, "fixture-streams", "1.0.0",
        "driver.elf", false, &scoped.resourceIdentity));
    GraphV2 resources(nativeProviderStreamHost()); assert(resources.addVerified(scoped));
    std::strcpy(scoped.resourceIdentity.id, "changed");
    auto lease = resources.acquire("fixture.streams", 1); assert(lease.slot);
    const auto* bound = static_cast<const provider_stream_fixture_api*>(resources.interfaceFor(lease));
    char content[4]{}; assert(bound && bound->read_resource(content) == T5_STREAM_OK);
    assert(!std::strcmp(content, "abc"));
    bound->block_quiesce(true); assert(!resources.release(lease));
    assert(bound->read_resource(content) == T5_STREAM_DENIED);
    bound->block_quiesce(false); assert(resources.release(lease));
    assert(resources.shutdown()); assert(gate.unpin(root)); assert(!gate.pinned(root));
  }
  puts("Loaded provider stream contexts, isolation, records and quiescence passed");
}
