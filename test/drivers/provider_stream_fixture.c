#include "provider_stream_fixture.h"
#include <assert.h>
static const risc_stream_provider_v1 *host;
static uint32_t source;
static bool blocked;
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
static const provider_stream_fixture_api api = {streams, source_handle, block_quiesce};
static const risc_driver_streams_v2 driver = {
    {RISC_PROVIDER_DRIVER_ABI_V2, sizeof(risc_driver_streams_v2),
     "fixture-streams", "fixture.streams", 1, &api, start, stop, quiesce},
    bind_streams
};
__attribute__((visibility("default")))
const risc_driver_v2 *t5_driver_get(uint32_t abi) {
    return abi == RISC_PROVIDER_DRIVER_ABI_V2 ? &driver.driver : 0;
}
