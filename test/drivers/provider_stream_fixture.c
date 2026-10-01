#include "provider_stream_fixture.h"
#include "RiscPackageResourcesV1.h"
#include <assert.h>
static const risc_stream_provider_v1 *host;
static uint32_t source;
static bool blocked;
static uint32_t poll_count, record_source, byte_sink, consumed_bytes;
static bool auto_enabled;
static uint32_t record_sink_handle, consumed_records;
static bool bind_streams(const risc_stream_provider_v1 *api) {
    assert(api && api->api_version == RISC_STREAM_PROVIDER_API_V1);
    assert(api->struct_size >= sizeof(*api) && api->context);
    host = api;
    return true;
}
static bool start(const risc_provider_dependency_v1 *deps, size_t count) {
    assert(!deps && !count && host);
    const risc_stream_endpoint_v1 spec = {sizeof(spec), 1, 1, 8, 0, 0, 0};
    assert(host->publish(host->context, &spec, &source) == 0 && source);
#ifdef REJECT_START
    return false;
#else
    return true;
#endif
}
static bool quiesce(void) {
    // Revocation must precede even unsuccessful quiescence. This callback is
    // executing inside the actual dynamically loaded fixture, not the host.
    uint32_t count = 99;
    assert(host->produce(host->context, source, "x", 1, &count) == -2 && count == 0);
    return !blocked;
}
static void stop(void) { assert(quiesce()); host = 0; source = 0; }
static const risc_stream_provider_v1 *streams(void) { return host; }
static uint32_t source_handle(void) { return source; }
static void block_quiesce(bool value) { blocked = value; }
static uint32_t polls(void) { return poll_count; }
static void automatic(bool enable) {
    if (enable && !record_source) {
        const risc_stream_endpoint_v1 record = {sizeof(record), 2, 1, 0, "fixture.record.v1", 8, 2};
        const risc_stream_endpoint_v1 sink = {sizeof(sink), 1, 2, 8, 0, 0, 0};
        assert(host->publish(host->context, &record, &record_source) == 0);
        assert(host->publish(host->context, &sink, &byte_sink) == 0);
        const risc_stream_endpoint_v1 record_sink = {sizeof(record_sink), 2, 2, 0, "fixture.record.v1", 8, 2};
        assert(host->publish(host->context, &record_sink, &record_sink_handle) == 0);
    }
    auto_enabled = enable;
}
static uint32_t records(void) { return record_source; }
static uint32_t sink(void) { return byte_sink; }
static uint32_t consumed(void) { return consumed_bytes; }
static uint32_t record_sink(void) { return record_sink_handle; }
static uint32_t records_consumed(void) { return consumed_records; }
static void poll(uint32_t budget) {
    assert(budget && budget <= 2); ++poll_count;
    if (!auto_enabled) return;
    uint32_t count = 0; char bytes[8];
    int result = host->produce(host->context, source, "P", 1, &count);
    assert(result == 0 || result == 1);
    result = host->produce_record(host->context, record_source, "R", 1);
    assert(result == 0 || result == 1);
    result = host->consume(host->context, byte_sink, bytes, sizeof(bytes), &count);
    assert(result == 0 || result == 1 || result == 2);
    consumed_bytes += count;
    result = host->consume_record(host->context, record_sink_handle, bytes, sizeof(bytes), &count);
    assert(result == 0 || result == 1 || result == 2);
    if (result == 0) ++consumed_records;
}
static int32_t read_resource(char* out) {
    if (!host || host->struct_size < sizeof(risc_stream_provider_resources_v1)) return -2;
    const risc_stream_provider_resources_v1* resources = (const risc_stream_provider_resources_v1*)host;
    uint32_t handle = 0, count = 0;
    int32_t result = resources->open_resource(host->context, "assets/text.txt", &handle);
    if (result != 0) return result;
    result = resources->read_resource(host->context, handle, out, 3, &count);
    if (host->close(host->context, handle) != 0) return -5;
    return result == 0 && count == 3 ? 0 : -5;
}
static const provider_stream_fixture_api api = {streams, source_handle, block_quiesce, polls, automatic, records, sink, consumed, record_sink, records_consumed, read_resource};
static const risc_driver_poll_v2 driver = {
    {{RISC_PROVIDER_DRIVER_ABI_V2, sizeof(risc_driver_poll_v2),
      "fixture-streams", "fixture.streams", 1, &api, start, stop, quiesce}, 0, bind_streams},
    poll
};
__attribute__((visibility("default")))
const risc_driver_v2 *t5_driver_get(uint32_t abi) {
    return abi == RISC_PROVIDER_DRIVER_ABI_V2 ? &driver.streams.driver : 0;
}
